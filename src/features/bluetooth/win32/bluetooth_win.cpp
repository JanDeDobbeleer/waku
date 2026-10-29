#include <windows.h>

#include "features/bluetooth/bluetooth.h"
// These SDK headers depend on the Windows declarations above.
#include <bluetoothapis.h>
#include <cfgmgr32.h>
#include <devicetopology.h>
#include <ks.h>
#include <ksproxy.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <propidl.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <set>
#include <thread>

#include "kernel/win32/winrt_apartment.h"

namespace wabar::bluetooth {

// --- pure helpers (bluetooth_types.py) -------------------------------------------------------

QString btDeviceTypeName(BtDeviceType type) {
  switch (type) {
    case BtDeviceType::Headphones:
      return QStringLiteral("headphones");
    case BtDeviceType::Headset:
      return QStringLiteral("headset");
    case BtDeviceType::Speaker:
      return QStringLiteral("speaker");
    case BtDeviceType::Phone:
      return QStringLiteral("phone");
    case BtDeviceType::Tablet:
      return QStringLiteral("tablet");
    case BtDeviceType::Laptop:
      return QStringLiteral("laptop");
    case BtDeviceType::Computer:
      return QStringLiteral("computer");
    case BtDeviceType::Keyboard:
      return QStringLiteral("keyboard");
    case BtDeviceType::Mouse:
      return QStringLiteral("mouse");
    case BtDeviceType::Controller:
      return QStringLiteral("controller");
    case BtDeviceType::Watch:
      return QStringLiteral("watch");
    case BtDeviceType::Camera:
      return QStringLiteral("camera");
    case BtDeviceType::Generic:
      break;
  }
  return QStringLiteral("generic");
}

QString formatBtAddress(quint64 address) {
  QStringList parts;
  for (int i = 5; i >= 0; --i) {
    parts
        << QStringLiteral("%1").arg((address >> (8 * i)) & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
  }
  return parts.join(QLatin1Char(':'));
}

std::pair<int, int> parseClassOfDevice(quint32 cod) {
  return {static_cast<int>((cod >> 8) & 0x1F), static_cast<int>((cod >> 2) & 0x3F)};
}

namespace {
constexpr int kMajorComputer = 1;
constexpr int kMajorPhone = 2;
constexpr int kMajorAudioVideo = 4;
constexpr int kMajorPeripheral = 5;
constexpr int kMajorImaging = 6;
constexpr int kMajorWearable = 7;
constexpr int kMajorToy = 8;
}  // namespace

BtDeviceType deviceTypeFromCod(int major, int minor) {
  namespace bt = winrt::Windows::Devices::Bluetooth;
  if (major == kMajorAudioVideo) {
    if (minor == 1 || minor == 2) return BtDeviceType::Headset;
    if (minor == 6 || minor == 7) return BtDeviceType::Headphones;
    return BtDeviceType::Speaker;
  }
  if (major == kMajorPhone) return BtDeviceType::Phone;
  if (major == kMajorComputer) {
    if (minor == static_cast<int>(bt::BluetoothMinorClass::ComputerTablet))
      return BtDeviceType::Tablet;
    if (minor == static_cast<int>(bt::BluetoothMinorClass::ComputerLaptop))
      return BtDeviceType::Laptop;
    return BtDeviceType::Computer;
  }
  if (major == kMajorPeripheral) {
    const int peripheral = (minor >> 4) & 0x3;
    if (peripheral == 1) return BtDeviceType::Keyboard;
    if (peripheral == 2) return BtDeviceType::Mouse;
    const int low = minor & 0x0F;
    if (low >= 2 && low <= 5) return BtDeviceType::Controller;
    return BtDeviceType::Generic;
  }
  if (major == kMajorWearable) return BtDeviceType::Watch;
  if (major == kMajorImaging) return (minor & 0x08) ? BtDeviceType::Camera : BtDeviceType::Generic;
  if (major == kMajorToy) {
    const int low = minor & 0x0F;
    if (low == 1 || low == 2 || low == 5) return BtDeviceType::Controller;
  }
  return BtDeviceType::Generic;
}

BtDeviceType deviceTypeFromLeAppearance(int category, int subcategory) {
  namespace bt = winrt::Windows::Devices::Bluetooth;
  if (category == bt::BluetoothLEAppearanceCategories::HumanInterfaceDevice()) {
    if (subcategory == bt::BluetoothLEAppearanceSubcategories::Keyboard())
      return BtDeviceType::Keyboard;
    if (subcategory == bt::BluetoothLEAppearanceSubcategories::Mouse()) return BtDeviceType::Mouse;
    if (subcategory == bt::BluetoothLEAppearanceSubcategories::Joystick() ||
        subcategory == bt::BluetoothLEAppearanceSubcategories::Gamepad()) {
      return BtDeviceType::Controller;
    }
    return BtDeviceType::Generic;
  }
  if (category == bt::BluetoothLEAppearanceCategories::Phone()) return BtDeviceType::Phone;
  if (category == bt::BluetoothLEAppearanceCategories::Computer()) return BtDeviceType::Computer;
  if (category == bt::BluetoothLEAppearanceCategories::Watch()) return BtDeviceType::Watch;
  return BtDeviceType::Generic;
}

QStringList profilesFromCod(int major, bool connected) {
  if (!connected) return {};
  if (major == kMajorAudioVideo) return {QStringLiteral("mic"), QStringLiteral("audio")};
  if (major == kMajorPeripheral) return {QStringLiteral("input")};
  return {};
}

bool preferDevice(const BluetoothDevice& candidate, const BluetoothDevice& existing) {
  const bool candidateKnown = candidate.paired || candidate.remembered;
  const bool existingKnown = existing.paired || existing.remembered;
  if (candidateKnown != existingKnown) return candidateKnown;
  if (candidate.connected != existing.connected) return candidate.connected;
  return false;
}

// --- Windows access --------------------------------------------------------------------------

namespace {

namespace bt = winrt::Windows::Devices::Bluetooth;
namespace enumeration = winrt::Windows::Devices::Enumeration;
namespace radios = winrt::Windows::Devices::Radios;

constexpr int kEnrichBatch = 6;
constexpr auto kSettleTimeout = std::chrono::seconds(10);
constexpr auto kSettlePoll = std::chrono::milliseconds(250);

// Bluetooth adapter device interface (BluetoothAdapter.GetDeviceSelector).
constexpr const wchar_t* kAdapterInterface = L"{92383b0e-f90e-4ac9-8d44-8c2d0d0ebda2}";

// Undocumented: DEVPKEY_Bluetooth_Battery, the PnP property that holds a device's battery level
// as a byte. Verified on Windows 11 build 26300 (ARM64); read failures yield "no battery".
const DEVPROPKEY kBluetoothBatteryKey = {
    {0x104EA319, 0x6EE2, 0x4701, {0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5}}, 2};

// Undocumented: KSPROPSETID_BtAudio, the kernel-streaming property set of the Bluetooth audio
// driver; ids 0 (reconnect) and 1 (disconnect) are one-shot requests. Verified on Windows 11
// build 26300 (ARM64); a missing or refusing driver reports "oneshot_failed".
const GUID kBtAudioPropertySet = {
    0x7FA06C40, 0xB8F6, 0x4C7E, {0x85, 0x56, 0xE8, 0xC3, 0x3A, 0x12, 0xE5, 0x4D}};
constexpr ULONG kOneshotReconnect = 0;
constexpr ULONG kOneshotDisconnect = 1;

QString toQString(const winrt::hstring& text) { return QString::fromWCharArray(text.c_str()); }

struct HandleCloser {
  void operator()(HANDLE handle) const {
    if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
  }
};

// Enumerates classic devices through BluetoothAPIs (BluetoothNativeApi.enumerate_devices).
void enumerateClassic(bool inquiry, std::map<QString, BluetoothDevice>& out) {
  BLUETOOTH_FIND_RADIO_PARAMS findParams{};
  findParams.dwSize = sizeof(findParams);
  HANDLE radio = nullptr;
  HBLUETOOTH_RADIO_FIND finder = BluetoothFindFirstRadio(&findParams, &radio);
  if (!finder) return;
  while (true) {
    std::unique_ptr<void, HandleCloser> radioGuard(radio);
    BLUETOOTH_DEVICE_SEARCH_PARAMS search{};
    search.dwSize = sizeof(search);
    search.fReturnAuthenticated = TRUE;
    search.fReturnRemembered = TRUE;
    search.fReturnUnknown = inquiry ? TRUE : FALSE;
    search.fReturnConnected = TRUE;
    search.fIssueInquiry = inquiry ? TRUE : FALSE;
    search.cTimeoutMultiplier = inquiry ? 2 : 1;
    search.hRadio = radio;
    BLUETOOTH_DEVICE_INFO info{};
    info.dwSize = sizeof(info);
    HBLUETOOTH_DEVICE_FIND deviceFinder = BluetoothFindFirstDevice(&search, &info);
    if (deviceFinder) {
      do {
        BluetoothDevice device;
        device.addressInt = info.Address.ullLong;
        device.address = formatBtAddress(device.addressInt);
        const auto [major, minor] = parseClassOfDevice(info.ulClassofDevice);
        device.name = QString::fromWCharArray(info.szName);
        if (device.name.isEmpty()) device.name = device.address;
        device.connected = info.fConnected != FALSE;
        device.paired = info.fAuthenticated != FALSE;
        device.remembered = info.fRemembered != FALSE;
        device.majorClass = major;
        device.deviceType = deviceTypeFromCod(major, minor);
        device.profiles = profilesFromCod(major, device.connected);
        const auto existing = out.find(device.address);
        if (existing == out.end() || preferDevice(device, existing->second))
          out[device.address] = device;
      } while (BluetoothFindNextDevice(deviceFinder, &info));
      BluetoothFindDeviceClose(deviceFinder);
    }
    radio = nullptr;
    if (!BluetoothFindNextRadio(finder, &radio)) break;
  }
  BluetoothFindRadioClose(finder);
}

std::vector<BluetoothDevice> listClassic(bool inquiry) {
  std::map<QString, BluetoothDevice> known;
  std::map<QString, BluetoothDevice> all;
  enumerateClassic(false, all);
  for (auto& [address, device] : all) {
    if (device.paired || device.remembered) known[address] = device;
  }
  if (inquiry) {
    std::map<QString, BluetoothDevice> found;
    enumerateClassic(true, found);
    for (auto& [address, device] : found) {
      if (known.count(address) || device.paired || device.remembered) continue;
      known[address] = device;
    }
  }
  std::vector<BluetoothDevice> result;
  for (auto& entry : known) result.push_back(entry.second);
  return result;
}

// Port of read_battery: HFP node first, then BTHENUM / BTHLE.
std::optional<int> deviceIdBattery(const std::wstring& filter, const QString& addressHex,
                                   bool& present) {
  std::optional<int> best;
  constexpr ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
  for (int attempt = 0; attempt < 2; ++attempt) {
    ULONG length = 0;
    if (CM_Get_Device_ID_List_SizeW(&length, filter.c_str(), flags) != CR_SUCCESS) return best;
    if (length <= 1 || length > 256 * 1024) return best;
    std::vector<wchar_t> buffer(length);
    const CONFIGRET status = CM_Get_Device_ID_ListW(filter.c_str(), buffer.data(), length, flags);
    if (status == CR_BUFFER_SMALL) continue;
    if (status != CR_SUCCESS) return best;
    for (const wchar_t* id = buffer.data(); *id; id += wcslen(id) + 1) {
      const QString deviceId = QString::fromWCharArray(id);
      if (!deviceId.toUpper().contains(addressHex)) continue;
      DEVINST devInst = 0;
      if (CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(id), 0) != CR_SUCCESS) continue;
      DEVPROPTYPE type = 0;
      BYTE value = 0;
      ULONG size = 1;
      std::optional<int> level;
      if (CM_Get_DevNode_PropertyW(devInst, &kBluetoothBatteryKey, &type, &value, &size, 0) ==
              CR_SUCCESS &&
          (type & DEVPROP_MASK_TYPE) == DEVPROP_TYPE_BYTE && value <= 100) {
        level = value;
      }
      present = true;
      if (level && !best) best = level;
    }
    return best;
  }
  return best;
}

std::optional<int> readBattery(quint64 address) {
  const QString addressHex = QStringLiteral("%1").arg(address, 12, 16, QLatin1Char('0')).toUpper();
  bool hfpPresent = false;
  std::optional<int> hfpLevel;
  std::optional<int> fallback;
  for (const wchar_t* filter : {L"BthHFEnum", L"BTHENUM", L"BTHLE"}) {
    bool present = false;
    const std::optional<int> level = deviceIdBattery(filter, addressHex, present);
    if (std::wstring(filter) == L"BthHFEnum") {
      hfpPresent = hfpPresent || present;
      if (level && !hfpLevel) hfpLevel = level;
    } else if (level && !fallback) {
      fallback = level;
    }
  }
  // An HFP node without a level yet means "unknown", not a stale BTHENUM/BTHLE value.
  return hfpPresent ? hfpLevel : fallback;
}

// radio_is_on: true = on, false = adapter present but off, nullopt = no adapter / query failed.
std::optional<bool> radioIsOn() {
  try {
    bool found = false;
    for (const auto& radio : radios::Radio::GetRadiosAsync().get()) {
      if (radio.Kind() != radios::RadioKind::Bluetooth) continue;
      found = true;
      if (radio.State() == radios::RadioState::On) return true;
    }
    if (found) return false;
    return std::nullopt;
  } catch (...) {
    return std::nullopt;
  }
}

bool setRadioPower(bool on) {
  try {
    if (radios::Radio::RequestAccessAsync().get() != radios::RadioAccessStatus::Allowed)
      return false;
    for (const auto& radio : radios::Radio::GetRadiosAsync().get()) {
      if (radio.Kind() != radios::RadioKind::Bluetooth) continue;
      const auto target = on ? radios::RadioState::On : radios::RadioState::Off;
      return radio.SetStateAsync(target).get() == radios::RadioAccessStatus::Allowed;
    }
  } catch (...) {
  }
  return false;
}

bool isAdapterId(const QString& id) {
  return id.toLower().contains(QString::fromWCharArray(kAdapterInterface));
}

bool isPairedDeviceId(const QString& id) {
  const QString upper = id.toUpper();
  if (upper.isEmpty() || isAdapterId(id)) return false;
  return upper.contains(QLatin1String("BTHLEDEVICE#")) ||
         upper.contains(QLatin1String("BTHLE#DEV_")) ||
         upper.contains(QLatin1String("BLUETOOTH#")) ||
         upper.contains(QLatin1String("BTHENUM\\")) || upper.contains(QLatin1String("BTHENUM#"));
}

// Fills name/type/connected/battery of classic or LE devices. WinRT lookups start in a batch.
void enrichDevices(std::vector<BluetoothDevice>& devices) {
  for (size_t begin = 0; begin < devices.size(); begin += kEnrichBatch) {
    const size_t end = std::min(devices.size(), begin + kEnrichBatch);
    std::vector<winrt::Windows::Foundation::IAsyncOperation<bt::BluetoothDevice>> classicOps(
        end - begin, nullptr);
    std::vector<winrt::Windows::Foundation::IAsyncOperation<bt::BluetoothLEDevice>> leOps(
        end - begin, nullptr);
    for (size_t i = begin; i < end; ++i) {
      if (!devices[i].addressInt) continue;
      try {
        if (devices[i].isLe)
          leOps[i - begin] =
              bt::BluetoothLEDevice::FromBluetoothAddressAsync(devices[i].addressInt);
        else
          classicOps[i - begin] =
              bt::BluetoothDevice::FromBluetoothAddressAsync(devices[i].addressInt);
      } catch (...) {
      }
    }
    for (size_t i = begin; i < end; ++i) {
      BluetoothDevice& device = devices[i];
      if (!device.addressInt) continue;
      if (device.isLe) {
        try {
          bt::BluetoothLEDevice le{nullptr};
          if (leOps[i - begin]) le = leOps[i - begin].get();
          if (!le) {
            device.connected = false;
          } else {
            device.connected = le.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected;
            if (const auto id = le.DeviceId(); !id.empty()) device.deviceId = toQString(id);
            if (const auto name = le.Name(); !name.empty()) device.name = toQString(name);
            if (const auto appearance = le.Appearance()) {
              device.deviceType =
                  deviceTypeFromLeAppearance(appearance.Category(), appearance.SubCategory());
              if (device.deviceType == BtDeviceType::Keyboard ||
                  device.deviceType == BtDeviceType::Mouse ||
                  device.deviceType == BtDeviceType::Controller) {
                device.majorClass = kMajorPeripheral;
              }
            }
            device.profiles = profilesFromCod(device.majorClass, device.connected);
          }
        } catch (...) {
          device.connected = false;
        }
      } else {
        try {
          bt::BluetoothDevice classicDevice{nullptr};
          if (classicOps[i - begin]) classicDevice = classicOps[i - begin].get();
          if (classicDevice) {
            if (const auto id = classicDevice.DeviceId(); !id.empty())
              device.deviceId = toQString(id);
            if (const auto name = classicDevice.Name(); !name.empty())
              device.name = toQString(name);
            device.connected =
                classicDevice.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected;
            if (const auto cod = classicDevice.ClassOfDevice()) {
              const int major = static_cast<int>(cod.MajorClass());
              const int minor = static_cast<int>(cod.MinorClass());
              device.majorClass = major;
              device.deviceType = deviceTypeFromCod(major, minor);
            }
            device.profiles = profilesFromCod(device.majorClass, device.connected);
          }
        } catch (...) {
        }
      }
      if (device.connected)
        device.battery = readBattery(device.addressInt);
      else
        device.battery.reset();
    }
  }
}

// list_le_devices: paired BLE devices known to Windows. nullopt = enumeration failed.
std::optional<std::vector<BluetoothDevice>> listLeDevices() {
  try {
    std::map<QString, BluetoothDevice> found;
    // Unfiltered enumeration like Python's find_all_async(), keeping only BLE device ids.
    for (const auto& info : enumeration::DeviceInformation::FindAllAsync().get()) {
      const QString id = toQString(info.Id());
      if (!id.contains(QLatin1String("BTHLEDEVICE#"), Qt::CaseInsensitive) &&
          !id.contains(QLatin1String("BTHLE#DEV_"), Qt::CaseInsensitive))
        continue;
      try {
        const auto le = bt::BluetoothLEDevice::FromIdAsync(info.Id()).get();
        if (!le) continue;
        const quint64 address = le.BluetoothAddress();
        if (!address) continue;
        const QString text = formatBtAddress(address);
        if (found.count(text)) continue;
        BluetoothDevice device;
        QString name = toQString(le.Name());
        if (name.isEmpty()) name = toQString(info.Name());
        name = name.trimmed();
        device.name = name.isEmpty() ? text : name;
        device.address = text;
        device.addressInt = address;
        device.paired = true;
        device.remembered = true;
        device.deviceId = toQString(le.DeviceId()).isEmpty() ? id : toQString(le.DeviceId());
        device.isLe = true;
        found[text] = device;
      } catch (...) {
      }
    }
    std::vector<BluetoothDevice> result;
    for (auto& entry : found) result.push_back(entry.second);
    return result;
  } catch (...) {
    return std::nullopt;
  }
}

// --- Bluetooth audio connect / disconnect (bluetooth_audio.py) --------------------------------

template <class T>
struct ComPtr {
  T* ptr = nullptr;
  ComPtr() = default;
  ComPtr(const ComPtr&) = delete;
  ComPtr& operator=(const ComPtr&) = delete;
  ComPtr(ComPtr&& other) noexcept : ptr(other.ptr) { other.ptr = nullptr; }
  ComPtr& operator=(ComPtr&& other) noexcept {
    if (this != &other) {
      if (ptr) ptr->Release();
      ptr = other.ptr;
      other.ptr = nullptr;
    }
    return *this;
  }
  ~ComPtr() {
    if (ptr) ptr->Release();
  }
  T** put() { return &ptr; }
  T* operator->() const { return ptr; }
  explicit operator bool() const { return ptr != nullptr; }
};

// PKEY_Device_FriendlyName ({a45c254e-df1c-4efd-8020-67d146a850e0}, 14), declared locally because
// the SDK header needs the property-key definition macros.
const PROPERTYKEY kFriendlyNameKey = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

struct AudioEndpoint {
  QString id;
  QString friendlyName;
  DWORD state = 0;
};

std::vector<AudioEndpoint> audioEndpoints(IMMDeviceEnumerator* enumerator) {
  std::vector<AudioEndpoint> result;
  ComPtr<IMMDeviceCollection> collection;
  if (FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATEMASK_ALL, collection.put())))
    return result;
  UINT count = 0;
  collection->GetCount(&count);
  for (UINT i = 0; i < count; ++i) {
    ComPtr<IMMDevice> device;
    if (FAILED(collection->Item(i, device.put()))) continue;
    AudioEndpoint endpoint;
    LPWSTR id = nullptr;
    if (SUCCEEDED(device->GetId(&id)) && id) {
      endpoint.id = QString::fromWCharArray(id);
      CoTaskMemFree(id);
    }
    device->GetState(&endpoint.state);
    ComPtr<IPropertyStore> store;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, store.put()))) {
      PROPVARIANT value;
      PropVariantInit(&value);
      if (SUCCEEDED(store->GetValue(kFriendlyNameKey, &value)) && value.vt == VT_LPWSTR &&
          value.pwszVal) {
        endpoint.friendlyName = QString::fromWCharArray(value.pwszVal);
      }
      PropVariantClear(&value);
    }
    result.push_back(endpoint);
  }
  return result;
}

// True if any matching endpoint is active, false if all are unplugged, nullopt if none match.
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

bool idHasMac(const QString& deviceId, const QString& mac) {
  if (mac.size() != 12) return false;
  QString compact = deviceId.toUpper();
  compact.remove(QLatin1Char(':')).remove(QLatin1Char('-')).remove(QLatin1Char('_'));
  return compact.contains(mac);
}

QString containerId(IMMDevice* device) {
  ComPtr<IPropertyStore> store;
  if (FAILED(device->OpenPropertyStore(STGM_READ, store.put()))) return {};
  // PKEY_Device_ContainerId: groups the render and capture endpoints of one physical device.
  const PROPERTYKEY key = {
      {0x8C7ED206, 0x3F8A, 0x4827, {0xB3, 0xAB, 0xAE, 0x9E, 0x1F, 0xAE, 0xFC, 0x6C}}, 2};
  PROPVARIANT value;
  PropVariantInit(&value);
  QString result;
  if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_CLSID && value.puuid) {
    wchar_t text[64] = {};
    StringFromGUID2(*value.puuid, text, 64);
    result = QString::fromWCharArray(text);
  }
  PropVariantClear(&value);
  return result;
}

// Sends the one-shot reconnect/disconnect KS property to every Bluetooth KS filter of the device.
// Returns an error reason, or an empty string on success.
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
    ComPtr<IMMDevice> device;
    if (FAILED(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(endpoint.id.utf16()), device.put())))
      continue;
    const QString container = containerId(device.ptr);
    ComPtr<IDeviceTopology> topology;
    if (FAILED(device->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr,
                                reinterpret_cast<void**>(topology.put())))) {
      continue;
    }
    UINT connectors = 0;
    topology->GetConnectorCount(&connectors);
    for (UINT c = 0; c < connectors; ++c) {
      ComPtr<IConnector> connector;
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
  std::vector<ComPtr<IKsControl>> controls;
  std::set<QString> seen;
  for (const Candidate& c : candidates) {
    if (!containers.empty()) {
      if (!containers.count(c.container)) continue;
    } else if (!c.seed) {
      continue;
    }
    if (!seen.insert(c.filterId).second) continue;
    ComPtr<IMMDevice> ksDevice;
    if (FAILED(
            enumerator->GetDevice(reinterpret_cast<LPCWSTR>(c.filterId.utf16()), ksDevice.put())))
      continue;
    controls.emplace_back();
    if (FAILED(ksDevice->Activate(__uuidof(IKsControl), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(controls.back().put())))) {
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

// --- worker ----------------------------------------------------------------------------------

struct DeviceWatch {
  bt::BluetoothDevice classic{nullptr};
  bt::BluetoothLEDevice le{nullptr};
  winrt::event_token token;
};

// Runs on the backend thread: every blocking call and all WinRT subscriptions live here.
class BluetoothWorker : public QObject {
  Q_OBJECT
 public:
  BluetoothWorker(std::atomic<bool>* connectRunning, std::atomic<bool>* radioRunning)
      : connectRunning_(connectRunning), radioRunning_(radioRunning) {}

 public slots:
  void start() {
    wabar::kernel::ensureWinrtApartment();
    started_ = true;
    startAdapterWatch();
    refreshLePending_ = true;
    refresh();
  }

  void shutdown() {
    started_ = false;
    cancel_ = true;
    stopAdapterWatch();
    clearDeviceWatchers();
    unsubscribeRadio();
    leCache_.clear();
    devices_.clear();
  }

  void cancelConnect() { cancel_ = true; }

  void scheduleRefresh() {
    if (refreshQueued_ || !started_) return;
    refreshQueued_ = true;
    QMetaObject::invokeMethod(this, &BluetoothWorker::refreshQueuedRun, Qt::QueuedConnection);
  }

  void refresh() {
    if (!started_) return;
    wabar::kernel::ensureWinrtApartment();
    const bool refreshLe = refreshLePending_ || leCache_.empty();
    refreshLePending_ = false;
    try {
      const std::optional<bool> radio = radioIsOn();
      if (!started_) return;
      // Only RadioState.Off counts as off: the WinRT radio can lag a dongle swap while devices
      // still enumerate.
      if (radio == std::optional<bool>(false)) {
        radioOn_ = false;
        devices_.clear();
        leCache_.clear();
        clearDeviceWatchers();
        bindRadio();
        publish();
        return;
      }
      std::vector<BluetoothDevice> devices = listDevices(false, refreshLe);
      if (!radio && devices.empty()) {
        radioOn_ = false;
        devices_.clear();
        leCache_.clear();
        clearDeviceWatchers();
        publish();
        return;
      }
      radioOn_ = true;
      devices_.clear();
      for (BluetoothDevice& device : devices) {
        if (!device.address.isEmpty()) devices_[device.address] = device;
      }
      bindRadio();
      syncDeviceWatchers(values());
      publish();
    } catch (const std::exception& error) {
      publish();
      emit refreshFailed(QString::fromUtf8(error.what()));
    } catch (...) {
      publish();
      emit refreshFailed(QStringLiteral("Bluetooth refresh failed"));
    }
  }

  void scan() {
    if (!started_) return;
    wabar::kernel::ensureWinrtApartment();
    emit scanStarted();
    try {
      const std::optional<bool> radio = radioIsOn();
      if (radio != std::optional<bool>(true)) {
        radioOn_ = false;
        devices_.clear();
        leCache_.clear();
        clearDeviceWatchers();
        publish();
        emit scanCompleted(static_cast<int>(BtScanStatus::RadioOff), {});
        return;
      }
      std::vector<BluetoothDevice> devices = listDevices(true, true);
      std::vector<BluetoothDevice> paired;
      for (const BluetoothDevice& device : devices) {
        if (device.paired || device.remembered) paired.push_back(device);
      }
      radioOn_ = true;
      devices_.clear();
      for (const BluetoothDevice& device : paired) {
        if (!device.address.isEmpty()) devices_[device.address] = device;
      }
      syncDeviceWatchers(paired);
      publish();
      emit scanCompleted(static_cast<int>(BtScanStatus::Success),
                         QVector<BluetoothDevice>(devices.begin(), devices.end()));
    } catch (...) {
      emit scanCompleted(static_cast<int>(BtScanStatus::Error), {});
    }
  }

  void setRadio(bool on) {
    wabar::kernel::ensureWinrtApartment();
    const bool ok = setRadioPower(on);
    if (!started_) {
      radioRunning_->store(false);
      return;
    }
    if (!ok) {
      radioOn_ = radioIsOn().value_or(false);
      if (!radioOn_) {
        devices_.clear();
        leCache_.clear();
        clearDeviceWatchers();
      }
      publish();
      emit refreshFailed(QStringLiteral("Unable to change Bluetooth power"));
      radioRunning_->store(false);
      return;
    }
    refreshLePending_ = true;
    refresh();
    radioRunning_->store(false);
  }

  void connectDevice(const QString& address, bool connect) {
    wabar::kernel::ensureWinrtApartment();
    const QString action = connect ? QStringLiteral("connect") : QStringLiteral("disconnect");
    const auto found = devices_.find(address);
    if (found == devices_.end()) {
      BluetoothDevice missing;
      missing.address = address;
      connectRunning_->store(false);
      emit connectionFinished(false, QStringLiteral("Device not found"), missing);
      return;
    }
    BluetoothDevice device = found->second;
    if (!device.supportsConnect()) {
      connectRunning_->store(false);
      emit connectionFinished(false, QStringLiteral("Use Windows Settings to manage this device"),
                              device);
      return;
    }
    cancel_ = false;
    QString error;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(enumerator.put())))) {
      connectRunning_->store(false);
      emit connectionFinished(false, connectError(action, QStringLiteral("no_ks_controls")),
                              device);
      return;
    }
    const auto audioBefore = audioIsConnected(enumerator.ptr, device.name);
    error = setAudioConnection(enumerator.ptr, device.addressInt, device.name, connect, cancel_);
    if (!started_) {
      connectRunning_->store(false);
      return;
    }
    const auto audio = audioIsConnected(enumerator.ptr, device.name);
    if (error.isEmpty() && audio != std::optional<bool>(connect)) error = QStringLiteral("timeout");
    // Idle audio with WinRT still reporting Connected is not a real disconnect.
    if (error.isEmpty() && !connect && audioBefore != std::optional<bool>(true)) {
      std::vector<BluetoothDevice> one{device};
      enrichDevices(one);
      device = one.front();
      devices_[address] = device;
      if (device.connected) error = QStringLiteral("timeout");
    }
    if (!error.isEmpty()) {
      finishConnect(device, false, connectError(action, error));
      return;
    }
    if (audio) {
      setConnected(device, *audio);
      if (device.connected && device.addressInt) device.battery = readBattery(device.addressInt);
      devices_[address] = device;
    }
    finishConnect(device, true, QStringLiteral("Device %1ed").arg(action));
  }

  void onAdapterChanged(const QString& kind) {
    if (!started_) return;
    wabar::kernel::ensureWinrtApartment();
    if (kind.startsWith(QLatin1String("Device"))) {
      refreshLePending_ = true;
      scheduleRefresh();
      return;
    }
    // Adapter events rebuild the state from scratch.
    unsubscribeRadio();
    clearDeviceWatchers();
    devices_.clear();
    leCache_.clear();
    radioOn_ = false;
    publish();
    refreshLePending_ = true;
    scheduleRefresh();
  }

  void onRadioChanged() {
    if (!started_) return;
    refreshLePending_ = true;
    scheduleRefresh();
  }

  void onDeviceConnectionChanged(quint64 address, bool connected) {
    if (!started_ || connectRunning_->load()) return;
    wabar::kernel::ensureWinrtApartment();
    const auto found = devices_.find(formatBtAddress(address));
    if (found == devices_.end()) {
      refreshLePending_ = true;
      scheduleRefresh();
      return;
    }
    BluetoothDevice& device = found->second;
    if (device.connected == connected) {
      if (connected && !device.battery) {
        device.battery = readBattery(device.addressInt);
        publish();
      }
      return;
    }
    setConnected(device, connected);
    if (connected) device.battery = readBattery(device.addressInt);
    publish();
  }

 signals:
  void statusReady(const wabar::bluetooth::BluetoothStatus& status);
  void refreshFailed(const QString& message);
  void scanStarted();
  void scanCompleted(int status, const QVector<wabar::bluetooth::BluetoothDevice>& devices);
  void connectionFinished(bool success, const QString& message,
                          const wabar::bluetooth::BluetoothDevice& device);

 private:
  void refreshQueuedRun() {
    refreshQueued_ = false;
    refresh();
  }

  std::vector<BluetoothDevice> values() const {
    std::vector<BluetoothDevice> result;
    for (const auto& entry : devices_) result.push_back(entry.second);
    return result;
  }

  void publish() {
    BluetoothStatus status;
    status.radioOn = radioOn_;
    for (const auto& entry : devices_) status.devices.push_back(entry.second);
    emit statusReady(status);
  }

  void setConnected(BluetoothDevice& device, bool connected) {
    device.connected = connected;
    device.profiles = profilesFromCod(device.majorClass, connected);
    if (!connected) device.battery.reset();
  }

  void finishConnect(const BluetoothDevice& device, bool success, const QString& message) {
    connectRunning_->store(false);
    emit connectionFinished(success, message, device);
    publish();
  }

  // list_devices: classic paired + BLE-only, then enriched.
  std::vector<BluetoothDevice> listDevices(bool inquiry, bool refreshLe) {
    std::map<QString, BluetoothDevice> byAddress;
    for (BluetoothDevice& device : listClassic(inquiry)) byAddress[device.address] = device;
    if (refreshLe || leCache_.empty()) {
      if (auto refreshed = listLeDevices()) {
        leCache_.clear();
        for (BluetoothDevice& device : *refreshed) leCache_[device.address] = device;
      }
    }
    for (const auto& entry : leCache_) {
      if (!byAddress.count(entry.first)) byAddress[entry.first] = entry.second;
    }
    std::vector<BluetoothDevice> result;
    for (auto& entry : byAddress) result.push_back(entry.second);
    enrichDevices(result);
    return result;
  }

  void bindRadio() {
    if (radioSubscribed_) return;
    try {
      for (const auto& radio : radios::Radio::GetRadiosAsync().get()) {
        if (radio.Kind() != radios::RadioKind::Bluetooth) continue;
        QPointer<BluetoothWorker> self(this);
        radioToken_ = radio.StateChanged([self](auto const&, auto const&) {
          if (self)
            QMetaObject::invokeMethod(self, &BluetoothWorker::onRadioChanged, Qt::QueuedConnection);
        });
        radio_ = radio;
        radioSubscribed_ = true;
        return;
      }
    } catch (...) {
    }
  }

  void unsubscribeRadio() {
    if (radio_) {
      try {
        radio_.StateChanged(radioToken_);
      } catch (...) {
      }
    }
    radio_ = nullptr;
    radioSubscribed_ = false;
  }

  void startAdapterWatch() {
    if (watcher_) return;
    try {
      watcher_ = enumeration::DeviceInformation::CreateWatcher();
      QPointer<BluetoothWorker> self(this);
      auto post = [self](const QString& kind) {
        if (self)
          QMetaObject::invokeMethod(
              self,
              [self, kind] {
                if (self) self->onAdapterChanged(kind);
              },
              Qt::QueuedConnection);
      };
      watchReady_ = std::make_shared<std::atomic<bool>>(false);
      auto ready = watchReady_;
      addedToken_ =
          watcher_.Added([post, ready](auto const&, enumeration::DeviceInformation const& info) {
            if (!ready->load()) return;
            const QString id = toQString(info.Id());
            if (isAdapterId(id))
              post(QStringLiteral("Added"));
            else if (isPairedDeviceId(id))
              post(QStringLiteral("DeviceAdded"));
          });
      removedToken_ = watcher_.Removed(
          [post, ready](auto const&, enumeration::DeviceInformationUpdate const& update) {
            if (!ready->load()) return;
            const QString id = toQString(update.Id());
            if (isAdapterId(id))
              post(QStringLiteral("Removed"));
            else if (isPairedDeviceId(id))
              post(QStringLiteral("DeviceRemoved"));
          });
      updatedToken_ = watcher_.Updated(
          [post, ready](auto const&, enumeration::DeviceInformationUpdate const& update) {
            if (!ready->load()) return;
            if (isAdapterId(toQString(update.Id()))) post(QStringLiteral("Updated"));
          });
      completedToken_ =
          watcher_.EnumerationCompleted([ready](auto const&, auto const&) { ready->store(true); });
      watcher_.Start();
    } catch (...) {
      watcher_ = nullptr;
    }
  }

  void stopAdapterWatch() {
    if (!watcher_) return;
    try {
      watcher_.Added(addedToken_);
      watcher_.Removed(removedToken_);
      watcher_.Updated(updatedToken_);
      watcher_.EnumerationCompleted(completedToken_);
      if (watcher_.Status() != enumeration::DeviceWatcherStatus::Stopped &&
          watcher_.Status() != enumeration::DeviceWatcherStatus::Aborted) {
        watcher_.Stop();
      }
    } catch (...) {
    }
    if (watchReady_) watchReady_->store(false);
    watcher_ = nullptr;
  }

  void syncDeviceWatchers(const std::vector<BluetoothDevice>& devices) {
    std::set<quint64> needed;
    for (const BluetoothDevice& device : devices) {
      if (device.addressInt) needed.insert(device.addressInt);
    }
    for (auto it = watches_.begin(); it != watches_.end();) {
      if (!needed.count(it->first)) {
        removeWatch(it->second);
        it = watches_.erase(it);
      } else {
        ++it;
      }
    }
    for (const BluetoothDevice& device : devices) {
      if (!device.addressInt || watches_.count(device.addressInt) || !started_) continue;
      if (auto watch = openWatch(device)) watches_[device.addressInt] = *watch;
    }
  }

  std::optional<DeviceWatch> openWatch(const BluetoothDevice& device) {
    try {
      QPointer<BluetoothWorker> self(this);
      DeviceWatch watch;
      if (device.isLe) {
        watch.le = bt::BluetoothLEDevice::FromBluetoothAddressAsync(device.addressInt).get();
        if (!watch.le) return std::nullopt;
        watch.token = watch.le.ConnectionStatusChanged(
            [self](bt::BluetoothLEDevice const& sender, auto const&) {
              postConnection(self, sender.BluetoothAddress(),
                             sender.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected);
            });
        return watch;
      }
      if (!device.deviceId.isEmpty() && !device.deviceId.contains(QLatin1String("BluetoothLE"))) {
        watch.classic =
            bt::BluetoothDevice::FromIdAsync(winrt::hstring(device.deviceId.toStdWString())).get();
      }
      if (!watch.classic)
        watch.classic = bt::BluetoothDevice::FromBluetoothAddressAsync(device.addressInt).get();
      if (!watch.classic) return std::nullopt;
      watch.token = watch.classic.ConnectionStatusChanged(
          [self](bt::BluetoothDevice const& sender, auto const&) {
            postConnection(self, sender.BluetoothAddress(),
                           sender.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected);
          });
      return watch;
    } catch (...) {
      return std::nullopt;
    }
  }

  static void postConnection(const QPointer<BluetoothWorker>& self, quint64 address,
                             bool connected) {
    if (!self) return;
    QMetaObject::invokeMethod(
        self,
        [self, address, connected] {
          if (self) self->onDeviceConnectionChanged(address, connected);
        },
        Qt::QueuedConnection);
  }

  static void removeWatch(DeviceWatch& watch) {
    try {
      if (watch.le) {
        watch.le.ConnectionStatusChanged(watch.token);
        watch.le.Close();
      } else if (watch.classic) {
        watch.classic.ConnectionStatusChanged(watch.token);
        watch.classic.Close();
      }
    } catch (...) {
    }
  }

  void clearDeviceWatchers() {
    for (auto& entry : watches_) removeWatch(entry.second);
    watches_.clear();
  }

  std::atomic<bool>* connectRunning_;
  std::atomic<bool>* radioRunning_;
  std::atomic<bool> cancel_{false};
  bool started_ = false;
  bool refreshQueued_ = false;
  bool refreshLePending_ = false;
  bool radioOn_ = false;
  bool radioSubscribed_ = false;
  std::map<QString, BluetoothDevice> devices_;
  std::map<QString, BluetoothDevice> leCache_;
  std::map<quint64, DeviceWatch> watches_;
  radios::Radio radio_{nullptr};
  winrt::event_token radioToken_;
  enumeration::DeviceWatcher watcher_{nullptr};
  winrt::event_token addedToken_;
  winrt::event_token removedToken_;
  winrt::event_token updatedToken_;
  winrt::event_token completedToken_;
  std::shared_ptr<std::atomic<bool>> watchReady_;
};

// GUI-thread facade: owns the worker thread and forwards calls to it.
class RealBluetoothBackend final : public BluetoothBackend {
 public:
  RealBluetoothBackend() {
    worker_ = new BluetoothWorker(&connectRunning_, &radioRunning_);
    worker_->moveToThread(&thread_);
    connect(worker_, &BluetoothWorker::statusReady, this,
            [this](const BluetoothStatus& status) { publishStatus(status); });
    connect(worker_, &BluetoothWorker::refreshFailed, this, &BluetoothBackend::refreshFailed);
    connect(worker_, &BluetoothWorker::scanStarted, this, &BluetoothBackend::scanStarted);
    connect(worker_, &BluetoothWorker::scanCompleted, this, &BluetoothBackend::scanCompleted);
    connect(worker_, &BluetoothWorker::connectionFinished, this,
            &BluetoothBackend::connectionFinished);
    thread_.setObjectName(QStringLiteral("BluetoothBackend"));
    thread_.start();
  }

  ~RealBluetoothBackend() override {
    if (thread_.isRunning()) {
      worker_->cancelConnect();
      QMetaObject::invokeMethod(worker_, &BluetoothWorker::shutdown, Qt::BlockingQueuedConnection);
      thread_.quit();
      thread_.wait();
    }
    delete worker_;
  }

  void start() override {
    if (started_) return;
    started_ = true;
    QMetaObject::invokeMethod(worker_, &BluetoothWorker::start, Qt::QueuedConnection);
  }
  void refresh() override {
    QMetaObject::invokeMethod(
        worker_, [w = worker_] { w->onRadioChanged(); }, Qt::QueuedConnection);
  }
  void scan() override {
    QMetaObject::invokeMethod(worker_, &BluetoothWorker::scan, Qt::QueuedConnection);
  }
  bool setRadio(bool on) override {
    if (connectRunning_.load() || radioRunning_.exchange(true)) return false;
    QMetaObject::invokeMethod(
        worker_, [w = worker_, on] { w->setRadio(on); }, Qt::QueuedConnection);
    return true;
  }
  void connectDevice(const QString& address) override { queueConnect(address, true); }
  void disconnectDevice(const QString& address) override { queueConnect(address, false); }

 private:
  void queueConnect(const QString& address, bool connect) {
    if (connectRunning_.exchange(true)) {
      BluetoothDevice device;
      device.address = address;
      emit connectionFinished(false, QStringLiteral("Another connection is in progress"), device);
      return;
    }
    QMetaObject::invokeMethod(
        worker_, [w = worker_, address, connect] { w->connectDevice(address, connect); },
        Qt::QueuedConnection);
  }

  QThread thread_;
  BluetoothWorker* worker_ = nullptr;
  std::atomic<bool> connectRunning_{false};
  std::atomic<bool> radioRunning_{false};
  bool started_ = false;
};

}  // namespace

std::unique_ptr<BluetoothBackend> createBluetoothBackend() {
  return std::make_unique<RealBluetoothBackend>();
}

bool hasBluetoothAdapter() {
  BLUETOOTH_FIND_RADIO_PARAMS findParams{};
  findParams.dwSize = sizeof(findParams);
  HANDLE radio = nullptr;
  HBLUETOOTH_RADIO_FIND finder = BluetoothFindFirstRadio(&findParams, &radio);
  if (!finder) return false;
  if (radio) CloseHandle(radio);
  BluetoothFindRadioClose(finder);
  return true;
}

}  // namespace wabar::bluetooth

#include "bluetooth_win.moc"
