#include "features/audio/audio_stream_source.h"

// clang-format off
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <winrt/base.h>
// clang-format on

#include <QDebug>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <functional>
#include <iterator>
#include <utility>

#include "kernel/qt/gui_post.h"
#include "kernel/win32/com_scope.h"

namespace wabar::audio {
namespace {

// Documented Core Audio only: IAudioSessionManager2, IAudioSessionControl2, ISimpleAudioVolume,
// IAudioSessionNotification, IAudioSessionEvents and IMMNotificationClient.

// kind 0: volume or mute changed, kind 1: the set of sessions or a session state changed, kind 2:
// a device changed (the default render endpoint may be another one), kind 3: a session's display
// name or icon changed.
using Notify = std::function<void(int kind)>;

template <typename Derived, typename Interface>
class ComObject : public Interface {
 public:
  ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = --refs_;
    if (remaining == 0) delete static_cast<Derived*>(this);
    return remaining;
  }
  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(Interface)) {
      *object = static_cast<Interface*>(this);
      AddRef();
      return S_OK;
    }
    *object = nullptr;
    return E_NOINTERFACE;
  }

 private:
  std::atomic<ULONG> refs_{1};
};

class SessionEvents final : public ComObject<SessionEvents, IAudioSessionEvents> {
 public:
  explicit SessionEvents(Notify notify) : notify_(std::move(notify)) {}
  HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override { return text(); }
  HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override { return text(); }
  HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float, BOOL, LPCGUID) override {
    if (notify_) notify_(0);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override {
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState) override { return structural(); }
  HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override {
    return structural();
  }

 private:
  HRESULT structural() {
    if (notify_) notify_(1);
    return S_OK;
  }
  HRESULT text() {
    if (notify_) notify_(3);
    return S_OK;
  }
  Notify notify_;
};

class SessionCreated final : public ComObject<SessionCreated, IAudioSessionNotification> {
 public:
  explicit SessionCreated(Notify notify) : notify_(std::move(notify)) {}
  HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override {
    if (notify_) notify_(1);
    return S_OK;
  }

 private:
  Notify notify_;
};

class DefaultDeviceEvents final : public ComObject<DefaultDeviceEvents, IMMNotificationClient> {
 public:
  explicit DefaultDeviceEvents(Notify notify) : notify_(std::move(notify)) {}
  HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
    if (notify_) notify_(2);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
    if (flow == eRender && role == eConsole && notify_) notify_(2);
    return S_OK;
  }
  HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override {
    return S_OK;
  }

 private:
  Notify notify_;
};

QString takeCoString(LPWSTR text) {
  const QString result = text ? QString::fromWCharArray(text) : QString();
  if (text) CoTaskMemFree(text);
  return result;
}

QString processImagePath(DWORD pid) {
  if (pid == 0) return {};
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!process) return {};
  wchar_t buffer[MAX_PATH * 2];
  DWORD size = static_cast<DWORD>(std::size(buffer));
  QString path;
  if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
    path = QString::fromWCharArray(buffer, static_cast<int>(size));
  }
  CloseHandle(process);
  return path;
}

// A display name that is a resource reference ("@%SystemRoot%\...,-100") is not text.
bool isUsableText(const QString& text) {
  return !text.isEmpty() && !text.startsWith(QLatin1Char('@'));
}

class SystemStreams final : public AudioStreamSource {
 public:
  explicit SystemStreams(QObject* parent) : AudioStreamSource(parent) {
    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                        __uuidof(IMMDeviceEnumerator), enumerator_.put_void());
    if (FAILED(hr)) {
      qWarning().noquote() << "AudioStreams: no device enumerator, hr" << Qt::hex << hr;
      return;
    }
    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(40);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { rebuildAndNotify(); });
    deviceEvents_ = new DefaultDeviceEvents(post());
    if (FAILED(enumerator_->RegisterEndpointNotificationCallback(deviceEvents_))) {
      deviceEvents_->Release();
      deviceEvents_ = nullptr;
    }
    rebuild();
  }

  ~SystemStreams() override { stop(); }

  void stop() override {
    refreshTimer_.stop();
    if (enumerator_ && deviceEvents_) {
      enumerator_->UnregisterEndpointNotificationCallback(deviceEvents_);
      deviceEvents_->Release();
      deviceEvents_ = nullptr;
    }
    release();
  }

  std::vector<AudioStreamInfo> streams() override {
    std::vector<AudioStreamInfo> result;
    result.reserve(entries_.size());
    for (const Entry& entry : entries_) result.push_back(read(entry));
    return result;
  }

  bool setVolume(const QString& id, double volume) override {
    const Entry* entry = find(id);
    return entry && entry->volume &&
           SUCCEEDED(entry->volume->SetMasterVolume(
               static_cast<float>(std::clamp(volume, 0.0, 1.0)), nullptr));
  }

  bool setMuted(const QString& id, bool muted) override {
    const Entry* entry = find(id);
    return entry && entry->volume &&
           SUCCEEDED(entry->volume->SetMute(muted ? TRUE : FALSE, nullptr));
  }

 private:
  struct Entry {
    QString id;
    DWORD pid = 0;
    // The display name and icon as shown (read once per session and on a text event).
    QString name;
    QString icon;
    winrt::com_ptr<IAudioSessionControl2> control;
    winrt::com_ptr<ISimpleAudioVolume> volume;
    SessionEvents* events = nullptr;  // registered on `control`; one reference held here
  };

  // The callback COM threads call: it only re-posts to the GUI thread. Core Audio may still call
  // it after an Unregister call failed (AUDCLNT_E_DEVICE_INVALIDATED, exactly when sessions
  // change), while a reload deletes the source; kernel::postToGui reads `self` on the GUI thread.
  Notify post() {
    QPointer<SystemStreams> self(this);
    return [self, this](int kind) { kernel::postToGui(self, [this, kind] { onEvent(kind); }); };
  }

  void onEvent(int kind) {
    if (kind == 0) {
      emit streamsChanged();
      return;
    }
    if (kind == 2) deviceEvent_ = true;
    if (kind == 3) textStale_ = true;
    // Sessions come and go in bursts; read them once after the burst.
    refreshTimer_.start();
  }

  void rebuildAndNotify() {
    // Another default endpoint means other sessions; otherwise only the changed ones are touched.
    if (deviceEvent_ && defaultEndpointId() != endpointId_) release();
    deviceEvent_ = false;
    if (textStale_) {
      textStale_ = false;
      for (Entry& entry : entries_) readText(entry);
    }
    rebuild();
    emit streamsChanged();
  }

  const Entry* find(const QString& id) const {
    for (const Entry& entry : entries_) {
      if (entry.id == id) return &entry;
    }
    return nullptr;
  }

  // The session's display name and icon, else the process's executable name and path.
  static void readText(Entry& entry) {
    LPWSTR rawName = nullptr;
    QString name;
    if (SUCCEEDED(entry.control->GetDisplayName(&rawName))) name = takeCoString(rawName);
    LPWSTR rawIcon = nullptr;
    QString icon;
    if (SUCCEEDED(entry.control->GetIconPath(&rawIcon))) icon = takeCoString(rawIcon);
    QString image;
    if (!isUsableText(name) || !isUsableText(icon)) image = processImagePath(entry.pid);
    if (!isUsableText(name)) {
      name = image.isEmpty() ? QStringLiteral("Unknown") : QFileInfo(image).completeBaseName();
    }
    entry.name = name;
    entry.icon = isUsableText(icon) ? icon : image;
  }

  // The text is cached; volume, mute and state are read live.
  AudioStreamInfo read(const Entry& entry) const {
    AudioStreamInfo info;
    info.id = entry.id;
    info.pid = entry.pid;
    info.name = entry.name;
    info.icon = entry.icon;
    float level = 1.0f;
    BOOL mute = FALSE;
    if (entry.volume) {
      entry.volume->GetMasterVolume(&level);
      entry.volume->GetMute(&mute);
    }
    info.volume = level;
    info.muted = mute != FALSE;
    AudioSessionState state = AudioSessionStateInactive;
    entry.control->GetState(&state);
    info.active = state == AudioSessionStateActive;
    return info;
  }

  static void unregister(Entry& entry) {
    if (entry.events) {
      if (entry.control) entry.control->UnregisterAudioSessionNotification(entry.events);
      entry.events->Release();
      entry.events = nullptr;
    }
  }

  void release() {
    for (Entry& entry : entries_) unregister(entry);
    entries_.clear();
    endpointId_.clear();
    if (manager_ && created_) manager_->UnregisterSessionNotification(created_);
    if (created_) {
      created_->Release();
      created_ = nullptr;
    }
    manager_ = nullptr;
  }

  QString defaultEndpointId() const {
    if (!enumerator_) return {};
    winrt::com_ptr<IMMDevice> device;
    if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, device.put()))) return {};
    LPWSTR rawId = nullptr;
    if (FAILED(device->GetId(&rawId))) return {};
    return takeCoString(rawId);
  }

  // Reads the default render endpoint's sessions. Sessions already known keep their entry, change
  // callback and cached text; only new ones are registered and read, and gone or expired ones are
  // unregistered. A session state change (every notification sound starts and stops one) thus
  // costs the enumeration, not a re-registration of every session.
  void rebuild() {
    if (!enumerator_) return;
    if (!manager_) {
      release();
      winrt::com_ptr<IMMDevice> device;
      if (FAILED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, device.put()))) return;
      if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                  manager_.put_void()))) {
        manager_ = nullptr;
        return;
      }
      LPWSTR rawId = nullptr;
      if (SUCCEEDED(device->GetId(&rawId))) endpointId_ = takeCoString(rawId);
      created_ = new SessionCreated(post());
      if (FAILED(manager_->RegisterSessionNotification(created_))) {
        created_->Release();
        created_ = nullptr;
      }
    }
    std::vector<Entry> next;
    winrt::com_ptr<IAudioSessionEnumerator> sessions;
    if (SUCCEEDED(manager_->GetSessionEnumerator(sessions.put()))) {
      int count = 0;
      sessions->GetCount(&count);
      for (int i = 0; i < count; ++i) {
        winrt::com_ptr<IAudioSessionControl> control;
        if (FAILED(sessions->GetSession(i, control.put()))) continue;
        auto control2 = control.try_as<IAudioSessionControl2>();
        if (!control2 || control2->IsSystemSoundsSession() == S_OK) continue;
        AudioSessionState state = AudioSessionStateExpired;
        if (FAILED(control2->GetState(&state)) || state == AudioSessionStateExpired) continue;
        LPWSTR rawId = nullptr;
        if (FAILED(control2->GetSessionInstanceIdentifier(&rawId))) continue;
        const QString id = takeCoString(rawId);
        const auto known = std::find_if(entries_.begin(), entries_.end(),
                                        [&](const Entry& entry) { return entry.id == id; });
        if (known != entries_.end()) {
          next.push_back(std::move(*known));
          entries_.erase(known);
          continue;
        }
        Entry entry;
        entry.id = id;
        control2->GetProcessId(&entry.pid);
        entry.volume = control.try_as<ISimpleAudioVolume>();
        entry.control = control2;
        readText(entry);
        entry.events = new SessionEvents(post());
        if (FAILED(entry.control->RegisterAudioSessionNotification(entry.events))) {
          entry.events->Release();
          entry.events = nullptr;
        }
        next.push_back(std::move(entry));
      }
    }
    for (Entry& gone : entries_) unregister(gone);
    entries_ = std::move(next);
  }

  // First, so the apartment outlives every COM object below; balanced when the object goes.
  const kernel::ComScope com_{kernel::ComApartment::SingleThreaded};
  winrt::com_ptr<IMMDeviceEnumerator> enumerator_;
  winrt::com_ptr<IAudioSessionManager2> manager_;
  DefaultDeviceEvents* deviceEvents_ = nullptr;
  SessionCreated* created_ = nullptr;
  std::vector<Entry> entries_;
  QTimer refreshTimer_;
  // The endpoint `manager_` belongs to; a device event that leaves it the default keeps it.
  QString endpointId_;
  bool deviceEvent_ = false;
  bool textStale_ = false;
};

}  // namespace

AudioStreamSource* createSystemAudioStreams(QObject* parent) { return new SystemStreams(parent); }

}  // namespace wabar::audio
