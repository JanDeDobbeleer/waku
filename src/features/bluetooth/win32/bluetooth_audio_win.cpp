// Connecting and disconnecting Bluetooth audio through the kernel-streaming one-shot
// properties of the Bluetooth audio driver (bluetooth_audio.py). Runs on the backend thread.
#include "features/bluetooth/win32/bluetooth_audio_win.h"

// clang-format off
#include <windows.h>
#include <devicetopology.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksproxy.h>
#include <mmdeviceapi.h>
#include <propidl.h>
#include <winrt/base.h>
// clang-format on

#include <chrono>
#include <set>
#include <thread>
#include <vector>

namespace waku::bluetooth {

namespace {

constexpr auto kSettleTimeout = std::chrono::seconds(10);
constexpr auto kSettlePoll = std::chrono::milliseconds(250);

// Undocumented: KSPROPSETID_BtAudio, the kernel-streaming property set of the Bluetooth audio
// driver; ids 0 (reconnect) and 1 (disconnect) are one-shot requests. Verified on Windows 11
// build 26300 (ARM64); a missing or refusing driver reports "oneshot_failed".
const GUID kBtAudioPropertySet = {
    0x7FA06C40, 0xB8F6, 0x4C7E, {0x85, 0x56, 0xE8, 0xC3, 0x3A, 0x12, 0xE5, 0x4D}};
constexpr ULONG kOneshotReconnect = 0;
constexpr ULONG kOneshotDisconnect = 1;

struct AudioEndpoint {
  QString id;
  QString friendlyName;
  DWORD state = 0;
};

std::vector<AudioEndpoint> audioEndpoints(IMMDeviceEnumerator* enumerator) {
  std::vector<AudioEndpoint> result;
  winrt::com_ptr<IMMDeviceCollection> collection;
  if (FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, collection.put())))
    return result;
  UINT count = 0;
  collection->GetCount(&count);
  for (UINT i = 0; i < count; ++i) {
    winrt::com_ptr<IMMDevice> device;
    if (FAILED(collection->Item(i, device.put()))) continue;
    AudioEndpoint endpoint;
    LPWSTR id = nullptr;
    if (SUCCEEDED(device->GetId(&id)) && id) {
      endpoint.id = QString::fromWCharArray(id);
      CoTaskMemFree(id);
    }
    device->GetState(&endpoint.state);
    winrt::com_ptr<IPropertyStore> store;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.put()))) {
      PROPVARIANT value;
      PropVariantInit(&value);
      if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR &&
          value.pwszVal) {
        endpoint.friendlyName = QString::fromWCharArray(value.pwszVal);
      }
      PropVariantClear(&value);
    }
    result.push_back(endpoint);
  }
  return result;
}

bool idHasMac(const QString& deviceId, const QString& mac) {
  if (mac.size() != 12) return false;
  QString compact = deviceId.toUpper();
  compact.remove(QLatin1Char(':')).remove(QLatin1Char('-')).remove(QLatin1Char('_'));
  return compact.contains(mac);
}

QString containerId(IMMDevice* device) {
  winrt::com_ptr<IPropertyStore> store;
  if (FAILED(device->OpenPropertyStore(STGM_READ, store.put()))) return {};
  // PKEY_Device_ContainerId groups the render and capture endpoints of one physical device.
  PROPVARIANT value;
  PropVariantInit(&value);
  QString result;
  if (SUCCEEDED(store->GetValue(PKEY_Device_ContainerId, &value)) && value.vt == VT_CLSID &&
      value.puuid) {
    wchar_t text[64] = {};
    StringFromGUID2(*value.puuid, text, 64);
    result = QString::fromWCharArray(text);
  }
  PropVariantClear(&value);
  return result;
}

}  // namespace

std::optional<bool> audioIsConnected(IMMDeviceEnumerator* enumerator, const QString& deviceName) {
  const QString needle = deviceName.trimmed().toCaseFolded();
  if (needle.isEmpty()) return std::nullopt;
  bool seen = false;
  for (const AudioEndpoint& endpoint : audioEndpoints(enumerator)) {
    if (!endpoint.friendlyName.toCaseFolded().contains(needle)) continue;
    seen = true;
    if (endpoint.state == DEVICE_STATE_ACTIVE) return true;
  }
  return seen ? std::optional<bool>(false) : std::nullopt;
}

QString setAudioConnection(IMMDeviceEnumerator* enumerator, quint64 addressInt,
                           const QString& deviceName, bool connect, std::atomic<bool>& cancel) {
  const auto current = audioIsConnected(enumerator, deviceName);
  if (!current) return QStringLiteral("not_audio_device");
  if (connect && *current) return {};

  struct Candidate {
    QString filterId;
    QString container;
    bool seed;
  };
  std::vector<Candidate> candidates;
  const QString mac = QStringLiteral("%1").arg(addressInt, 12, 16, QLatin1Char('0')).toUpper();
  const QString needle = deviceName.trimmed().toCaseFolded();
  for (const AudioEndpoint& endpoint : audioEndpoints(enumerator)) {
    winrt::com_ptr<IMMDevice> device;
    if (FAILED(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(endpoint.id.utf16()), device.put())))
      continue;
    const QString container = containerId(device.get());
    winrt::com_ptr<IDeviceTopology> topology;
    if (FAILED(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr,
                                topology.put_void()))) {
      continue;
    }
    UINT connectors = 0;
    topology->GetConnectorCount(&connectors);
    for (UINT c = 0; c < connectors; ++c) {
      winrt::com_ptr<IConnector> connector;
      if (FAILED(topology->GetConnector(c, connector.put()))) continue;
      LPWSTR other = nullptr;
      if (FAILED(connector->GetDeviceIdConnectedTo(&other)) || !other) continue;
      const QString otherId = QString::fromWCharArray(other);
      CoTaskMemFree(other);
      if (!otherId.toLower().contains(QLatin1String("bth"))) continue;
      const bool nameOk =
          !needle.isEmpty() && endpoint.friendlyName.toCaseFolded().contains(needle);
      candidates.push_back({otherId, container, idHasMac(otherId, mac) || nameOk});
    }
  }
  std::set<QString> containers;
  for (const Candidate& c : candidates) {
    if (c.seed && !c.container.isEmpty()) containers.insert(c.container);
  }
  std::vector<winrt::com_ptr<IKsControl>> controls;
  std::set<QString> seen;
  for (const Candidate& c : candidates) {
    if (!containers.empty()) {
      if (!containers.count(c.container)) continue;
    } else if (!c.seed) {
      continue;
    }
    if (!seen.insert(c.filterId).second) continue;
    winrt::com_ptr<IMMDevice> ksDevice;
    if (FAILED(
            enumerator->GetDevice(reinterpret_cast<LPCWSTR>(c.filterId.utf16()), ksDevice.put())))
      continue;
    controls.emplace_back();
    if (FAILED(ksDevice->Activate(__uuidof(IKsControl), CLSCTX_ALL, nullptr,
                                  controls.back().put_void()))) {
      controls.pop_back();
    }
  }
  if (controls.empty()) return QStringLiteral("no_ks_controls");

  KSPROPERTY property{};
  property.Set = kBtAudioPropertySet;
  property.Id = connect ? kOneshotReconnect : kOneshotDisconnect;
  property.Flags = KSPROPERTY_TYPE_GET;
  bool sent = false;
  for (auto& control : controls) {
    ULONG returned = 0;
    if (SUCCEEDED(control->KsProperty(&property, sizeof(property), nullptr, 0, &returned)))
      sent = true;
  }
  if (!sent) return QStringLiteral("oneshot_failed");

  const auto deadline = std::chrono::steady_clock::now() + kSettleTimeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (cancel.load()) return QStringLiteral("cancelled");
    if (audioIsConnected(enumerator, deviceName) == std::optional<bool>(connect)) return {};
    std::this_thread::sleep_for(kSettlePoll);
  }
  return QStringLiteral("timeout");
}

QString connectError(const QString& action, const QString& reason) {
  QString capitalized = action;
  capitalized[0] = capitalized[0].toUpper();
  if (reason == QLatin1String("cancelled")) return capitalized + QStringLiteral(" cancelled");
  if (reason == QLatin1String("not_audio_device") || reason == QLatin1String("no_ks_controls")) {
    return QStringLiteral("Failed to %1 device (no audio path; use Windows Settings)").arg(action);
  }
  if (reason == QLatin1String("oneshot_failed")) {
    return QStringLiteral("Failed to %1 device (audio driver request failed)").arg(action);
  }
  if (reason == QLatin1String("timeout") && action == QLatin1String("connect"))
    return QStringLiteral("Failed to connect device. Is it powered on and in range?");
  return QStringLiteral("Failed to %1 device").arg(action);
}

}  // namespace waku::bluetooth
