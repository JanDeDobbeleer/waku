#include "platform/audio.h"

// clang-format off
// functiondiscoverykeys_devpkey.h needs mmdeviceapi.h (propkey) first; keep this order.
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <winrt/base.h>
// clang-format on

#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <utility>

namespace yasb::platform {
namespace {

// Undocumented IPolicyConfig (used by SetDefaultEndpoint, the same call pycaw makes). Only
// SetDefaultEndpoint is called; the other slots keep the vtable layout (docs/architecture.md).
struct __declspec(uuid("f8679f50-850a-41cf-9c72-430f290290c8")) IPolicyConfig : IUnknown {
  virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, void**) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, BOOL, void**) = 0;
  virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, void*, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, BOOL, void*, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, void*) = 0;
  virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR, const PROPERTYKEY*, PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR, const PROPERTYKEY*, PROPVARIANT*) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR, ERole) = 0;
  virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, BOOL) = 0;
};

// {870af99c-171d-4f9e-af0d-e63df40c2bc9}
const CLSID kClsidPolicyConfigClient = {
    0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

class VolumeCallback final : public IAudioEndpointVolumeCallback {
 public:
  explicit VolumeCallback(std::function<void()> notify) : notify_(std::move(notify)) {}
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --refs_;
    if (remaining == 0) delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioEndpointVolumeCallback)) {
      *object = static_cast<IAudioEndpointVolumeCallback*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
    if (notify_) notify_();
    return S_OK;
  }

 private:
  std::atomic<ULONG> refs_{1};
  std::function<void()> notify_;
};

class DeviceCallback final : public IMMNotificationClient {
 public:
  // kind: 0 default changed (id = new default), 1 state changed (state = new state).
  using Notify = std::function<void(int kind, EDataFlow flow, ERole role, QString id, DWORD state)>;
  explicit DeviceCallback(Notify notify) : notify_(std::move(notify)) {}
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --refs_;
    if (remaining == 0) delete this;
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
      *object = static_cast<IMMNotificationClient*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }
  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR id, DWORD state) override {
    if (notify_) notify_(1, eRender, eConsole, id ? QString::fromWCharArray(id) : QString(), state);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role,
                                                   LPCWSTR id) override {
    if (notify_) notify_(0, flow, role, id ? QString::fromWCharArray(id) : QString(), 0);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
    return S_OK;
  }

 private:
  std::atomic<ULONG> refs_{1};
  Notify notify_;
};

QString takeCoString(LPWSTR text) {
  const QString result = text ? QString::fromWCharArray(text) : QString();
  if (text) CoTaskMemFree(text);
  return result;
}

class SystemAudio final : public AudioEndpoints {
 public:
  explicit SystemAudio(AudioFlow flow, QObject* parent)
      : AudioEndpoints(parent), flow_(flow == AudioFlow::Render ? eRender : eCapture) {
    winrt::com_ptr<IMMDeviceEnumerator> enumerator;
    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), enumerator.put_void());
    if (FAILED(hr)) {
      qWarning().noquote() << "AudioEndpoints: no device enumerator, hr" << Qt::hex << hr;
      return;
    }
    enumerator_ = enumerator;
    QPointer<SystemAudio> self(this);
    deviceCallback_ = new DeviceCallback(
        [self, this](int kind, EDataFlow flow, ERole role, QString id, DWORD state) {
          QObject* target = self.data();
          if (!target) return;
          QMetaObject::invokeMethod(
              target,
              [self, this, kind, flow, role, id, state] {
                if (self) onDeviceEvent(kind, flow, role, id, state);
              },
              Qt::QueuedConnection);
        });
    if (FAILED(enumerator_->RegisterEndpointNotificationCallback(deviceCallback_))) {
      qWarning() << "AudioEndpoints: could not register the device callback";
      deviceCallback_->Release();
      deviceCallback_ = nullptr;
    }
    resolveEndpoint();
  }

  ~SystemAudio() override { stop(); }

  void stop() override {
    if (enumerator_ && deviceCallback_) {
      enumerator_->UnregisterEndpointNotificationCallback(deviceCallback_);
      deviceCallback_->Release();
      deviceCallback_ = nullptr;
    }
    releaseVolume();
  }

  bool available() override { return ensureVolume() != nullptr; }

  std::optional<int> volumePercent() override {
    IAudioEndpointVolume* volume = ensureVolume();
    float scalar = 0;
    if (!volume || FAILED(volume->GetMasterVolumeLevelScalar(&scalar))) return std::nullopt;
    return static_cast<int>(std::lround(scalar * 100.0f));
  }

  bool setVolumePercent(int percent) override {
    IAudioEndpointVolume* volume = ensureVolume();
    return volume && SUCCEEDED(volume->SetMasterVolumeLevelScalar(
                         static_cast<float>(percent) / 100.0f, nullptr));
  }

  std::optional<bool> muted() override {
    IAudioEndpointVolume* volume = ensureVolume();
    BOOL mute = FALSE;
    if (!volume || FAILED(volume->GetMute(&mute))) return std::nullopt;
    return mute != FALSE;
  }

  bool setMuted(bool mute) override {
    IAudioEndpointVolume* volume = ensureVolume();
    return volume && SUCCEEDED(volume->SetMute(mute ? TRUE : FALSE, nullptr));
  }

  std::vector<AudioDevice> devices() override {
    std::vector<AudioDevice> result;
    if (!enumerator_) return result;
    winrt::com_ptr<IMMDeviceCollection> collection;
    if (FAILED(enumerator_->EnumAudioEndpoints(flow_, DEVICE_STATE_ACTIVE, collection.put())))
      return result;
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
      winrt::com_ptr<IMMDevice> device;
      if (FAILED(collection->Item(i, device.put()))) continue;
      LPWSTR rawId = nullptr;
      if (FAILED(device->GetId(&rawId))) continue;
      AudioDevice entry;
      entry.id = takeCoString(rawId);
      winrt::com_ptr<IPropertyStore> store;
      if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.put()))) {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
            value.pwszVal) {
          entry.name = QString::fromWCharArray(value.pwszVal);
        }
        PropVariantClear(&value);
      }
      result.push_back(std::move(entry));
    }
    return result;
  }

  QString defaultDeviceId() override {
    if (!enumerator_) return {};
    winrt::com_ptr<IMMDevice> device;
    if (FAILED(enumerator_->GetDefaultAudioEndpoint(flow_, eConsole, device.put()))) return {};
    LPWSTR rawId = nullptr;
    if (FAILED(device->GetId(&rawId))) return {};
    return takeCoString(rawId);
  }

  bool setDefaultDevice(const QString& id) override {
    winrt::com_ptr<IPolicyConfig> policy;
    HRESULT hr = CoCreateInstance(kClsidPolicyConfigClient, nullptr, CLSCTX_ALL,
                                  __uuidof(IPolicyConfig), policy.put_void());
    if (FAILED(hr)) {
      qWarning().noquote() << "Failed to set default audio device: no PolicyConfig, hr" << Qt::hex
                           << hr;
      return false;
    }
    hr = policy->SetDefaultEndpoint(reinterpret_cast<PCWSTR>(id.utf16()), eConsole);
    if (FAILED(hr)) {
      qWarning().noquote() << "Failed to set default audio device, hr" << Qt::hex << hr;
      return false;
    }
    return true;
  }

 private:
  void releaseVolume() {
    if (volume_ && volumeCallback_) volume_->UnregisterControlChangeNotify(volumeCallback_);
    if (volumeCallback_) {
      volumeCallback_->Release();
      volumeCallback_ = nullptr;
    }
    volume_ = nullptr;
  }

  IAudioEndpointVolume* ensureVolume() {
    if (!volume_ && enumerator_) resolveEndpoint();
    return volume_.get();
  }

  // Finds the default endpoint's volume interface and hooks its change callback. The interface
  // stays referenced for as long as the callback is registered (as in the Python service).
  void resolveEndpoint() {
    releaseVolume();
    if (!enumerator_) return;
    winrt::com_ptr<IMMDevice> device;
    if (FAILED(enumerator_->GetDefaultAudioEndpoint(flow_, eConsole, device.put()))) return;
    winrt::com_ptr<IAudioEndpointVolume> volume;
    if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                volume.put_void()))) {
      return;
    }
    QPointer<SystemAudio> self(this);
    auto* callback = new VolumeCallback([self, this] {
      QObject* target = self.data();
      if (!target) return;
      QMetaObject::invokeMethod(
          target,
          [self, this] {
            if (self) emit volumeChanged();
          },
          Qt::QueuedConnection);
    });
    if (FAILED(volume->RegisterControlChangeNotify(callback))) {
      qWarning() << "AudioEndpoints: could not register the volume callback";
      callback->Release();
    } else {
      volumeCallback_ = callback;
    }
    volume_ = volume;
  }

  void onDeviceEvent(int kind, EDataFlow flow, ERole role, const QString& id, DWORD state) {
    if (kind == 0) {
      if (flow != flow_ || role != eConsole) return;
      if (id == lastDefault_) return;
      lastDefault_ = id;
    } else {
      if (state != DEVICE_STATE_ACTIVE && state != DEVICE_STATE_DISABLED &&
          state != DEVICE_STATE_UNPLUGGED) {
        return;
      }
      const auto it = lastState_.find(id);
      if (it != lastState_.end() && it->second == state) return;
      lastState_[id] = state;
    }
    // The old interface is bound to the previous device; resolve the new default.
    resolveEndpoint();
    emit deviceChanged();
  }

  EDataFlow flow_;
  winrt::com_ptr<IMMDeviceEnumerator> enumerator_;
  winrt::com_ptr<IAudioEndpointVolume> volume_;
  VolumeCallback* volumeCallback_ = nullptr;
  DeviceCallback* deviceCallback_ = nullptr;
  QString lastDefault_;
  std::map<QString, DWORD> lastState_;
};

}  // namespace

AudioEndpoints* createSystemAudio(AudioFlow flow, QObject* parent) {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // already initialised is fine
  return new SystemAudio(flow, parent);
}

}  // namespace yasb::platform
