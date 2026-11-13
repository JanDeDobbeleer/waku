#include <windows.h>

#include "features/bluetooth/bluetooth.h"
// These SDK headers depend on the Windows declarations above.
#include <bluetoothapis.h>
#include <cfgmgr32.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <propidl.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "features/bluetooth/win32/bluetooth_audio_win.h"
#include "kernel/win32/winrt_apartment.h"
#include "kernel/win32/winrt_strings.h"

namespace waku::bluetooth {

// --- Windows access --------------------------------------------------------------------------

namespace {

namespace bt = winrt::Windows::Devices::Bluetooth;
namespace enumeration = winrt::Windows::Devices::Enumeration;
namespace radios = winrt::Windows::Devices::Radios;

constexpr int kEnrichBatch = 6;
// The "peripheral" major device class (Bluetooth assigned numbers), as in bluetooth_types_win.cpp.
constexpr int kMajorPeripheral = 5;

// Bluetooth adapter device interface (BluetoothAdapter.GetDeviceSelector).
constexpr const wchar_t* kAdapterInterface = L"{92383b0e-f90e-4ac9-8d44-8c2d0d0ebda2}";

// Undocumented: DEVPKEY_Bluetooth_Battery, the PnP property that holds a device's battery level
// as a byte. Verified on Windows 11 build 26300 (ARM64); read failures yield "no battery".
const DEVPROPKEY kBluetoothBatteryKey = {
    {0x104EA319, 0x6EE2, 0x4701, {0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5}}, 2};

using kernel::toQString;

// DEVPKEY_Device_FriendlyName (devpkey.h, documented), spelled out because the key lives in no
// library.
const DEVPROPKEY kFriendlyNameKey = {
    {0xA45C254E, 0xDF1C, 0x4EFD, {0x80, 0x20, 0x67, 0xD1, 0x46, 0xA8, 0x50, 0xE0}}, 14};

// The name Windows keeps on the PnP node of a device (BTHENUM\DEV_<address> for a classic device,
// BTHLE\DEV_<address> for an LE one). It is there whether or not the device is connected, where
// the Bluetooth APIs return "Bluetooth <address>" for a device that is not. Empty when there is
// none.
QString pnpFriendlyName(quint64 addressInt, bool isLe) {
  if (!addressInt) return {};
  const QString hex = QStringLiteral("%1").arg(addressInt, 12, 16, QLatin1Char('0')).toUpper();
  const std::wstring filter = isLe ? L"BTHLE" : L"BTHENUM";
  const QString wanted = QStringLiteral("%1\\DEV_%2")
                             .arg(isLe ? QStringLiteral("BTHLE") : QStringLiteral("BTHENUM"), hex);
  constexpr ULONG flags = CM_GETIDLIST_FILTER_ENUMERATOR | CM_GETIDLIST_FILTER_PRESENT;
  for (int attempt = 0; attempt < 2; ++attempt) {
    ULONG length = 0;
    if (CM_Get_Device_ID_List_SizeW(&length, filter.c_str(), flags) != CR_SUCCESS) return {};
    if (length <= 1 || length > 256 * 1024) return {};
    std::vector<wchar_t> buffer(length);
    const CONFIGRET status = CM_Get_Device_ID_ListW(filter.c_str(), buffer.data(), length, flags);
    if (status == CR_BUFFER_SMALL) continue;
    if (status != CR_SUCCESS) return {};
    for (const wchar_t* id = buffer.data(); *id; id += wcslen(id) + 1) {
      if (!QString::fromWCharArray(id).toUpper().startsWith(wanted)) continue;
      DEVINST devInst = 0;
      if (CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(id), 0) != CR_SUCCESS) continue;
      DEVPROPTYPE type = 0;
      ULONG size = 0;
      if (CM_Get_DevNode_PropertyW(devInst, &kFriendlyNameKey, &type, nullptr, &size, 0) !=
              CR_BUFFER_SMALL ||
          type != DEVPROP_TYPE_STRING || size < sizeof(wchar_t) || size > 1024) {
        continue;
      }
      std::vector<BYTE> value(size);
      if (CM_Get_DevNode_PropertyW(devInst, &kFriendlyNameKey, &type, value.data(), &size, 0) !=
          CR_SUCCESS) {
        continue;
      }
      const QString name =
          QString::fromWCharArray(reinterpret_cast<const wchar_t*>(value.data())).trimmed();
      if (!name.isEmpty()) return name;
    }
    return {};
  }
  return {};
}

// The container id shared by the classic and LE nodes of one physical device. Cached per device id:
// the WinRT lookup costs a round trip. Empty when Windows reports none.
QString containerIdFor(const QString& deviceId) {
  // Never destroyed: a retiring worker (one a reload let go without waiting) can still be in here
  // after main() has returned and the CRT has run the static destructors.
  static std::mutex& lock = *new std::mutex;
  static std::map<QString, QString>& cache = *new std::map<QString, QString>;
  {
    std::lock_guard<std::mutex> guard(lock);
    const auto found = cache.find(deviceId);
    if (found != cache.end()) return found->second;
  }
  QString result;
  try {
    const auto keys = winrt::single_threaded_vector<winrt::hstring>(
        {winrt::hstring(L"System.Devices.Aep.ContainerId"),
         winrt::hstring(L"System.Devices.ContainerId")});
    const auto info =
        enumeration::DeviceInformation::CreateFromIdAsync(kernel::toHString(deviceId), keys).get();
    for (const winrt::hstring& key : keys) {
      const auto value = info.Properties().TryLookup(key);
      if (!value) continue;
      const winrt::guid id = winrt::unbox_value_or<winrt::guid>(value, winrt::guid{});
      if (id == winrt::guid{}) continue;
      result = QUuid(id.Data1, id.Data2, id.Data3, id.Data4[0], id.Data4[1], id.Data4[2],
                     id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7])
                   .toString(QUuid::WithoutBraces);
      break;
    }
  } catch (...) {
    return {};
  }
  if (!result.isEmpty()) {
    std::lock_guard<std::mutex> guard(lock);
    cache[deviceId] = result;
  }
  return result;
}

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
        if (isPlaceholderBtName(device.name, device.address)) {
          const QString pnp = pnpFriendlyName(device.addressInt, false);
          device.name = pnp.isEmpty() ? device.address : pnp;
        }
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

// WAKU_BT_DEBUG=1 logs every refresh decision (radio result, counts, branch, thread).
bool btDebug() {
  static const bool on = qEnvironmentVariableIntValue("WAKU_BT_DEBUG") != 0;
  return on;
}

// "hresult 0x80070005 Access is denied." for logs and error texts.
QString hresultText(const winrt::hresult_error& error) {
  return QStringLiteral("hresult 0x%1 %2")
      .arg(static_cast<quint32>(error.code().value), 8, 16, QLatin1Char('0'))
      .arg(toQString(error.message()));
}

thread_local QString lastRadioError;

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
  } catch (const winrt::hresult_error& error) {
    lastRadioError = hresultText(error);
    return std::nullopt;
  } catch (...) {
    lastRadioError = QStringLiteral("unknown exception");
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
// Stops early, leaving the rest as they are, once `running` (when given) turns false.
void enrichDevices(std::vector<BluetoothDevice>& devices,
                   const std::atomic<bool>* running = nullptr) {
  for (size_t begin = 0; begin < devices.size(); begin += kEnrichBatch) {
    if (running && !running->load()) return;
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
      if (running && !running->load()) return;
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
      if (isPlaceholderBtName(device.name, device.address)) {
        const QString pnp = pnpFriendlyName(device.addressInt, device.isLe);
        if (!pnp.isEmpty()) device.name = pnp;
      }
      // The WinRT calls above can take seconds: a worker told to stop meanwhile ends here.
      if (running && !running->load()) return;
      if (device.containerId.isEmpty() && !device.deviceId.isEmpty())
        device.containerId = containerIdFor(device.deviceId);
      if (device.connected)
        device.battery = readBattery(device.addressInt);
      else
        device.battery.reset();
    }
  }
}

// list_le_devices: paired BLE devices known to Windows. nullopt = enumeration failed.
// The paired-LE selector returns one object per device. The unfiltered FindAllAsync() Python uses
// also returns every GATT service interface (about 1900 objects, 10 s) and opening each one took
// 95 s on a Surface Pro 11, which held the first Bluetooth snapshot back. Gives nullopt once
// `running` turns false.
std::optional<std::vector<BluetoothDevice>> listLeDevices(const std::atomic<bool>& running) {
  try {
    std::map<QString, BluetoothDevice> found;
    QElapsedTimer timer;
    timer.start();
    bool filtered = true;
    enumeration::DeviceInformationCollection all{nullptr};
    try {
      all = enumeration::DeviceInformation::FindAllAsync(
                bt::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true))
                .get();
    } catch (...) {
      filtered = false;
    }
    if (!all) all = enumeration::DeviceInformation::FindAllAsync().get();
    if (btDebug())
      qWarning() << "BTDBG LE list" << all.Size() << "filtered" << filtered << timer.elapsed()
                 << "ms";
    for (const auto& info : all) {
      if (!running.load()) return std::nullopt;
      const QString id = toQString(info.Id());
      if (!filtered && !id.contains(QLatin1String("BTHLEDEVICE#"), Qt::CaseInsensitive) &&
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
    if (btDebug()) qWarning() << "BTDBG LE found" << found.size() << timer.elapsed() << "ms";
    std::vector<BluetoothDevice> result;
    for (auto& entry : found) result.push_back(entry.second);
    return result;
  } catch (...) {
    return std::nullopt;
  }
}

// --- worker ----------------------------------------------------------------------------------

struct DeviceWatch {
  bt::BluetoothDevice classic{nullptr};
  bt::BluetoothLEDevice le{nullptr};
  winrt::event_token token;
};

// Runs on the backend thread: every blocking call and all WinRT subscriptions live here.
constexpr auto kPairTimeout = std::chrono::seconds(60);
constexpr auto kPairPoll = std::chrono::milliseconds(100);

// The text for a pairing that did not end Paired. The statuses that mean Windows needs its own
// ceremony UI (a PIN, a passkey comparison, a confirmation) all give the same advice.
QString pairingFailure(enumeration::DevicePairingResultStatus status) {
  using Status = enumeration::DevicePairingResultStatus;
  switch (status) {
    case Status::RequiredHandlerNotRegistered:
    case Status::RejectedByHandler:
    case Status::AuthenticationNotAllowed:
    case Status::InvalidCeremonyData:
    case Status::ProtectionLevelCouldNotBeMet:
      return waku::bluetooth::pairNeedsSettingsMessage();
    case Status::PairingCanceled:
      return QStringLiteral("Pairing cancelled");
    case Status::AuthenticationTimeout:
      return QStringLiteral("Pairing timed out");
    case Status::ConnectionRejected:
      return QStringLiteral("The device rejected the connection");
    case Status::NotReadyToPair:
      return QStringLiteral("The device is not ready to pair");
    case Status::AccessDenied:
      return QStringLiteral("Windows denied access to pairing");
    case Status::OperationAlreadyInProgress:
      return QStringLiteral("Windows is already pairing a device");
    default:
      return QStringLiteral("Pairing failed");
  }
}

// The DeviceInformation of a device by address (documented WinRT), or an empty object.
enumeration::DeviceInformation deviceInformationFor(const waku::bluetooth::BluetoothDevice& d) {
  if (d.isLe) {
    const auto le = bt::BluetoothLEDevice::FromBluetoothAddressAsync(d.addressInt).get();
    return le ? le.DeviceInformation() : nullptr;
  }
  const auto classic = bt::BluetoothDevice::FromBluetoothAddressAsync(d.addressInt).get();
  return classic ? classic.DeviceInformation() : nullptr;
}

// How WinRT delegates reach the worker. They run on WinRT pool threads and can still run after
// their token was revoked (revoking does not wait for a running delegate, and `DeviceWatcher.Stop`
// is asynchronous), so they never hold the worker: a QPointer tested on the pool thread and then
// posted to races with the GUI thread deleting the worker. `post` holds `mutex_` across the post,
// and ~BluetoothWorker calls `close` first, so once `close` returns no post can reach the worker.
// A post queued before that is dropped with the worker's event queue.
class WorkerRelay {
 public:
  explicit WorkerRelay(QObject* worker) : worker_(worker) {}
  // Queues `fn` on the worker's thread, or drops it when the worker is gone.
  template <class Fn>
  void post(Fn fn) {
    std::lock_guard lock(mutex_);
    if (worker_) QMetaObject::invokeMethod(worker_, std::move(fn), Qt::QueuedConnection);
  }
  void close() {
    std::lock_guard lock(mutex_);
    worker_ = nullptr;
  }

 private:
  std::mutex mutex_;
  QObject* worker_;
};

// A connect, pair or radio change in progress; shared by the backend and its worker.
struct BusyFlags {
  std::atomic<bool> connect{false};
  std::atomic<bool> radio{false};
};

class BluetoothWorker : public QObject {
  Q_OBJECT
 public:
  // The flags are shared with the backend, which may be gone while this worker still finishes.
  explicit BluetoothWorker(std::shared_ptr<BusyFlags> flags)
      : flags_(std::move(flags)),
        connectRunning_(&flags_->connect),
        radioRunning_(&flags_->radio),
        relay_(std::make_shared<WorkerRelay>(this)) {}
  // Runs on the GUI thread after the worker thread was joined (or never started).
  ~BluetoothWorker() override { relay_->close(); }

 public slots:
  void start() {
    waku::kernel::ensureWinrtApartment();
    // stopSoon() may run on the GUI thread at any point; it sets stopping_ before it clears
    // started_, so either this sees stopping_ or stopSoon's clear comes after this store.
    started_ = true;
    if (stopping_) {
      started_ = false;
      return;
    }
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
  // Not a slot: the GUI thread calls it while pairDevice() blocks this thread.
  void cancelPairing() { pairCancel_ = true; }
  // Not a slot: the GUI thread calls it before it waits for shutdown(), so whatever runs on this
  // thread (a pair, a refresh, the first refresh of start()) ends after the WinRT call in progress
  // instead of running to the end.
  void stopSoon() {
    stopping_ = true;
    started_ = false;
    cancel_ = true;
    pairCancel_ = true;
  }

  void scheduleRefresh() {
    if (refreshQueued_ || !started_) return;
    refreshQueued_ = true;
    QMetaObject::invokeMethod(this, &BluetoothWorker::refreshQueuedRun, Qt::QueuedConnection);
  }

  void refresh() {
    if (!started_) return;
    waku::kernel::ensureWinrtApartment();
    const bool refreshLe = refreshLePending_ || leCache_.empty();
    refreshLePending_ = false;
    try {
      lastRadioError.clear();
      const std::optional<bool> radio = radioIsOn();
      if (btDebug()) {
        qWarning().noquote() << "BTDBG refresh thread" << QThread::currentThread()->objectName()
                             << "radio" << (radio ? (*radio ? "on" : "off") : "unknown") << "error"
                             << lastRadioError << "refreshLe" << refreshLe;
      }
      if (!started_) return;
      // Only RadioState.Off counts as off: the WinRT radio can lag a dongle swap while devices
      // still enumerate.
      if (radio == std::optional<bool>(false)) {
        radioOn_ = false;
        radioUnknown_ = false;
        devices_.clear();
        leCache_.clear();
        clearDeviceWatchers();
        bindRadio();
        publish();
        return;
      }
      // Phase 1: classic devices (and the cached LE ones) are quick, so they go out first.
      std::vector<BluetoothDevice> devices = listDevices(false, false);
      if (!started_) return;
      if (btDebug()) {
        qWarning().noquote() << "BTDBG phase1" << devices.size() << "radio unknown" << !radio;
      }
      if (!radio && devices.empty() && leCache_.empty()) {
        // Nothing yet: the stack may not be ready. Keep the last state and say so; the service
        // retries and publishes "off" only when the retries are exhausted.
        radioUnknown_ = true;
      } else {
        radioUnknown_ = false;
        radioOn_ = true;
        applyDevices(devices);
      }
      publish();
      // Phase 2: the LE listing can be slow; it must not hold phase 1 back.
      if (refreshLe) {
        if (auto refreshed = listLeDevices(started_)) {
          if (!started_) return;
          leCache_.clear();
          for (BluetoothDevice& device : *refreshed) leCache_[device.address] = device;
          devices = listDevices(false, false);
        }
        if (!started_) return;
        if (devices.empty() && !radio) {
          radioUnknown_ = true;
        } else {
          radioUnknown_ = false;
          radioOn_ = true;
          applyDevices(devices);
        }
        if (btDebug()) {
          qWarning().noquote() << "BTDBG phase2" << devices.size() << "unknown" << radioUnknown_;
        }
        publish();
      }
    } catch (const winrt::hresult_error& error) {
      // Not a std::exception: without this clause the HRESULT was lost in the catch-all.
      const QString text = hresultText(error);
      qWarning().noquote() << "Bluetooth: refresh failed:" << text;
      publish();
      emit refreshFailed(QStringLiteral("Bluetooth refresh failed: ") + text);
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
    waku::kernel::ensureWinrtApartment();
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
      if (auto refreshed = listLeDevices(started_)) {
        leCache_.clear();
        for (BluetoothDevice& device : *refreshed) leCache_[device.address] = device;
      }
      std::vector<BluetoothDevice> devices = listDevices(true, false);
      std::vector<BluetoothDevice> paired;
      nearby_.clear();
      for (const BluetoothDevice& device : mergeSameDevices(devices)) {
        if (!device.paired && !device.remembered) nearby_[device.address] = device;
      }
      for (const BluetoothDevice& device : devices) {
        if (device.paired || device.remembered) paired.push_back(device);
      }
      radioOn_ = true;
      devices_.clear();
      for (const BluetoothDevice& device : mergeSameDevices(paired)) {
        if (!device.address.isEmpty()) devices_[device.address] = device;
      }
      syncDeviceWatchers(paired);
      publish();
      const std::vector<BluetoothDevice> merged = mergeSameDevices(devices);
      emit scanCompleted(static_cast<int>(BtScanStatus::Success),
                         QVector<BluetoothDevice>(merged.begin(), merged.end()));
    } catch (const winrt::hresult_error& error) {
      qWarning().noquote() << "Bluetooth: scan failed:" << hresultText(error);
      emit scanCompleted(static_cast<int>(BtScanStatus::Error), {});
    } catch (const std::exception& error) {
      qWarning().noquote() << "Bluetooth: scan failed:" << error.what();
      emit scanCompleted(static_cast<int>(BtScanStatus::Error), {});
    } catch (...) {
      qWarning() << "Bluetooth: scan failed";
      emit scanCompleted(static_cast<int>(BtScanStatus::Error), {});
    }
  }

  void setRadio(bool on) {
    waku::kernel::ensureWinrtApartment();
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

  // Pairs (`pair`) or forgets a device with the documented DeviceInformationPairing. The default
  // pairing has no PIN UI of ours: a device that needs one fails with the "pair in Windows
  // Settings" message. Blocks this thread until the operation ends, is cancelled
  // (cancelPairing) or runs out of time; the busy flag is cleared on every path.
  void pairDevice(const QString& address, bool pair) {
    waku::kernel::ensureWinrtApartment();
    pairCancel_ = false;
    BluetoothDevice device;
    device.address = address;
    const auto known = devices_.find(address);
    const auto foundNearby = nearby_.find(address);
    if (pair && foundNearby != nearby_.end())
      device = foundNearby->second;
    else if (known != devices_.end())
      device = known->second;
    else if (foundNearby != nearby_.end())
      device = foundNearby->second;
    if (device.addressInt == 0) {
      finishPairing(false, QStringLiteral("Device not found"), device);
      return;
    }
    try {
      const enumeration::DeviceInformation info = deviceInformationFor(device);
      if (!info) {
        finishPairing(false, QStringLiteral("Device not found"), device);
        return;
      }
      const enumeration::DeviceInformationPairing pairing = info.Pairing();
      if (!pair) {
        auto operation = pairing.UnpairAsync();
        if (!waitForPairing(operation)) {
          finishPairing(false,
                        pairCancel_ ? QStringLiteral("Pairing cancelled")
                                    : QStringLiteral("Forgetting the device timed out"),
                        device);
          return;
        }
        using Status = enumeration::DeviceUnpairingResultStatus;
        const Status status = operation.GetResults().Status();
        if (status != Status::Unpaired && status != Status::AlreadyUnpaired) {
          finishPairing(false, QStringLiteral("Windows could not forget the device"), device);
          return;
        }
        devices_.erase(address);
        for (const QString& other : device.otherAddresses) devices_.erase(other);
        leCache_.erase(address);
        device.paired = false;
        device.remembered = false;
        device.connected = false;
        publish();
        finishPairing(true, QStringLiteral("Device forgotten"), device);
        return;
      }
      if (pairing.IsPaired()) {
        finishPairing(true, QStringLiteral("Device paired"), device);
        return;
      }
      if (!pairing.CanPair()) {
        finishPairing(false, waku::bluetooth::pairNeedsSettingsMessage(), device);
        return;
      }
      auto operation = pairing.PairAsync();
      if (!waitForPairing(operation)) {
        finishPairing(
            false,
            pairCancel_ ? QStringLiteral("Pairing cancelled") : QStringLiteral("Pairing timed out"),
            device);
        return;
      }
      using Status = enumeration::DevicePairingResultStatus;
      const Status status = operation.GetResults().Status();
      if (status != Status::Paired && status != Status::AlreadyPaired) {
        finishPairing(false, pairingFailure(status), device);
        return;
      }
      device.paired = true;
      nearby_.erase(address);
      refreshLePending_ = true;
      refresh();
      finishPairing(true, QStringLiteral("Device paired"), device);
    } catch (const winrt::hresult_error&) {
      finishPairing(false, QStringLiteral("Windows could not reach the device"), device);
    } catch (...) {
      finishPairing(false, QStringLiteral("Pairing failed"), device);
    }
  }

  static QString audioText(const std::optional<bool>& state) {
    if (!state) return QStringLiteral("none");
    return *state ? QStringLiteral("active") : QStringLiteral("unplugged");
  }

  void connectDevice(const QString& address, bool connect) {
    waku::kernel::ensureWinrtApartment();
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
    winrt::com_ptr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), enumerator.put_void()))) {
      connectRunning_->store(false);
      emit connectionFinished(false, connectError(action, QStringLiteral("no_ks_controls")),
                              device);
      return;
    }
    QElapsedTimer attempt;
    attempt.start();
    // The endpoints are resolved once: finding them walks every endpoint's topology, which is slow
    // enough to be seen in the spinner when repeated for every poll.
    const std::vector<QString> endpointIds =
        audioEndpointIds(enumerator.get(), device.name, device.addressInt);
    const auto audioBefore = audioEndpointsConnected(enumerator.get(), endpointIds);
    error = setAudioConnection(enumerator.get(), device.addressInt, device.name, connect, cancel_,
                               endpointIds);
    if (!started_) {
      connectRunning_->store(false);
      return;
    }
    const auto audio = audioEndpointsConnected(enumerator.get(), endpointIds);
    if (error.isEmpty() && audio != std::optional<bool>(connect)) error = QStringLiteral("timeout");
    // Idle audio with WinRT still reporting Connected is not a real disconnect.
    if (error.isEmpty() && !connect && audioBefore != std::optional<bool>(true)) {
      std::vector<BluetoothDevice> one{device};
      enrichDevices(one, &started_);
      device = one.front();
      devices_[address] = device;
      if (device.connected) error = QStringLiteral("timeout");
    }
    qInfo().noquote()
        << QStringLiteral(
               "Bluetooth: audio %1 of %2 (%3): endpoints=%4 before=%5 after=%6 reason=%7 in %8 ms")
               .arg(action, device.name, device.address)
               .arg(endpointIds.size())
               .arg(audioText(audioBefore), audioText(audio),
                    error.isEmpty() ? QStringLiteral("ok") : error)
               .arg(attempt.elapsed());
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
    waku::kernel::ensureWinrtApartment();
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
    waku::kernel::ensureWinrtApartment();
    const QString text = formatBtAddress(address);
    const auto found = devices_.find(text);
    if (found == devices_.end()) {
      const bool merged = std::any_of(devices_.begin(), devices_.end(), [&](const auto& entry) {
        return entry.second.otherAddresses.contains(text);
      });
      // A merged-away entry changed: rebuild the row, no new LE listing needed.
      if (!merged) refreshLePending_ = true;
      scheduleRefresh();
      return;
    }
    if (!found->second.otherAddresses.isEmpty()) {
      scheduleRefresh();  // the row combines several entries: recompute it
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
  void statusReady(const waku::bluetooth::BluetoothStatus& status);
  void refreshFailed(const QString& message);
  void scanStarted();
  void scanCompleted(int status, const QVector<waku::bluetooth::BluetoothDevice>& devices);
  void connectionFinished(bool success, const QString& message,
                          const waku::bluetooth::BluetoothDevice& device);
  void pairingFinished(bool success, const QString& message,
                       const waku::bluetooth::BluetoothDevice& device);

 private:
  // Waits for a pairing operation in short steps so a cancel or the timeout is honoured. Returns
  // true when the operation completed; on a cancel or timeout it cancels the operation and returns
  // false.
  template <class Operation>
  bool waitForPairing(const Operation& operation) {
    const auto deadline = std::chrono::steady_clock::now() + kPairTimeout;
    while (operation.Status() == winrt::Windows::Foundation::AsyncStatus::Started) {
      if (pairCancel_ || !started_ || std::chrono::steady_clock::now() >= deadline) {
        operation.Cancel();
        return false;
      }
      operation.wait_for(kPairPoll);
    }
    return operation.Status() == winrt::Windows::Foundation::AsyncStatus::Completed;
  }

  void finishPairing(bool success, const QString& message, const BluetoothDevice& device) {
    connectRunning_->store(false);
    emit pairingFinished(success, message, device);
  }

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
    status.radioUnknown = radioUnknown_;
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

  // list_devices: classic paired + the cached BLE-only devices, then enriched. The LE listing is
  // separate (listLeDevices) because it is slow.
  std::vector<BluetoothDevice> listDevices(bool inquiry, bool) {
    std::map<QString, BluetoothDevice> byAddress;
    for (BluetoothDevice& device : listClassic(inquiry)) byAddress[device.address] = device;
    for (const auto& entry : leCache_) {
      if (!byAddress.count(entry.first)) byAddress[entry.first] = entry.second;
    }
    std::vector<BluetoothDevice> result;
    for (auto& entry : byAddress) result.push_back(entry.second);
    enrichDevices(result, &started_);
    return result;
  }

  void applyDevices(const std::vector<BluetoothDevice>& devices) {
    if (btDebug()) {
      for (const BluetoothDevice& device : devices) {
        qWarning().noquote() << "BTDBG entry" << device.name << device.address
                             << (device.isLe ? "LE" : "classic")
                             << btDeviceTypeName(device.deviceType) << "container"
                             << device.containerId << "connected" << device.connected;
      }
    }
    devices_.clear();
    for (const BluetoothDevice& device : mergeSameDevices(devices)) {
      if (!device.address.isEmpty()) devices_[device.address] = device;
    }
    bindRadio();
    // Watch every address, merged away or not: an LE connection change must reach the row.
    syncDeviceWatchers(devices);
  }

  void bindRadio() {
    if (radioSubscribed_) return;
    try {
      for (const auto& radio : radios::Radio::GetRadiosAsync().get()) {
        if (radio.Kind() != radios::RadioKind::Bluetooth) continue;
        radioToken_ = radio.StateChanged([relay = relay_, worker = this](auto const&, auto const&) {
          relay->post([worker] { worker->onRadioChanged(); });
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
      auto post = [relay = relay_, worker = this](const QString& kind) {
        relay->post([worker, kind] { worker->onAdapterChanged(kind); });
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
      DeviceWatch watch;
      if (device.isLe) {
        watch.le = bt::BluetoothLEDevice::FromBluetoothAddressAsync(device.addressInt).get();
        if (!watch.le) return std::nullopt;
        watch.token = watch.le.ConnectionStatusChanged(
            [relay = relay_, worker = this](bt::BluetoothLEDevice const& sender, auto const&) {
              postConnection(*relay, worker, sender.BluetoothAddress(),
                             sender.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected);
            });
        return watch;
      }
      if (!device.deviceId.isEmpty() && !device.deviceId.contains(QLatin1String("BluetoothLE"))) {
        watch.classic = bt::BluetoothDevice::FromIdAsync(kernel::toHString(device.deviceId)).get();
      }
      if (!watch.classic)
        watch.classic = bt::BluetoothDevice::FromBluetoothAddressAsync(device.addressInt).get();
      if (!watch.classic) return std::nullopt;
      watch.token = watch.classic.ConnectionStatusChanged(
          [relay = relay_, worker = this](bt::BluetoothDevice const& sender, auto const&) {
            postConnection(*relay, worker, sender.BluetoothAddress(),
                           sender.ConnectionStatus() == bt::BluetoothConnectionStatus::Connected);
          });
      return watch;
    } catch (...) {
      return std::nullopt;
    }
  }

  static void postConnection(WorkerRelay& relay, BluetoothWorker* worker, quint64 address,
                             bool connected) {
    relay.post(
        [worker, address, connected] { worker->onDeviceConnectionChanged(address, connected); });
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

  std::shared_ptr<BusyFlags> flags_;
  std::atomic<bool>* connectRunning_;
  std::atomic<bool>* radioRunning_;
  std::atomic<bool> cancel_{false};
  std::atomic<bool> pairCancel_{false};
  std::map<QString, BluetoothDevice> nearby_;  // unpaired devices of the last scan
  // Written by this thread (start, shutdown) and by the GUI thread (stopSoon); read by both.
  std::atomic<bool> started_{false};
  std::atomic<bool> stopping_{false};  // set once, by stopSoon
  bool refreshQueued_ = false;
  bool refreshLePending_ = false;
  bool radioOn_ = false;
  bool radioUnknown_ = false;
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
  // Shared with every WinRT delegate; see WorkerRelay.
  std::shared_ptr<WorkerRelay> relay_;
};

// Worker threads whose backend is gone but whose WinRT call has not returned yet. The process
// waits for them at exit (bounded), so none outlives QCoreApplication by accident. GUI thread.
QList<QPointer<QThread>>& retiringThreads() {
  static QList<QPointer<QThread>> threads;
  return threads;
}

void waitForRetiringThreads() {
  for (const QPointer<QThread>& thread : std::as_const(retiringThreads())) {
    if (thread) thread->wait(2000);
  }
}

// GUI-thread facade: owns the worker thread and forwards calls to it.
//
// The destructor does not wait for the worker. A WinRT call in progress (an enumeration or a
// connect) can take 30 s, and the destructor runs on the GUI thread while the engine clears its
// singletons on a reload: waiting froze the bar for that long. It asks the worker to stop, queues
// shutdown() and the thread's quit, and hands the thread and the worker to the GUI event loop,
// which deletes them when the thread has finished. The next backend may start meanwhile; the old
// worker does nothing after its call returns (stopSoon()).
class RealBluetoothBackend final : public BluetoothBackend {
 public:
  RealBluetoothBackend() {
    thread_ = new QThread;
    worker_ = new BluetoothWorker(flags_);
    worker_->moveToThread(thread_);
    connect(worker_, &BluetoothWorker::statusReady, this,
            [this](const BluetoothStatus& status) { publishStatus(status); });
    connect(worker_, &BluetoothWorker::refreshFailed, this, &BluetoothBackend::refreshFailed);
    connect(worker_, &BluetoothWorker::scanStarted, this, &BluetoothBackend::scanStarted);
    connect(worker_, &BluetoothWorker::scanCompleted, this, &BluetoothBackend::scanCompleted);
    connect(worker_, &BluetoothWorker::connectionFinished, this,
            &BluetoothBackend::connectionFinished);
    connect(worker_, &BluetoothWorker::pairingFinished, this, &BluetoothBackend::pairingFinished);
    thread_->setObjectName(QStringLiteral("BluetoothBackend"));
    thread_->start();
  }

  ~RealBluetoothBackend() override {
    if (!thread_->isRunning()) {
      delete worker_;
      delete thread_;
      return;
    }
    // Ends a running pair, connect or refresh after the WinRT call in progress.
    worker_->stopSoon();
    QThread* thread = thread_;
    BluetoothWorker* worker = worker_;
    // finished() comes from the worker thread; the context object lives on the GUI thread, so the
    // deletes run there, after the thread has left its event loop (as before). Connected before
    // the quit is queued: an idle worker can quit and emit finished() before this line runs, and
    // the worker and the thread would then never be deleted.
    QObject::connect(thread, &QThread::finished, thread, [thread, worker] {
      delete worker;
      thread->deleteLater();
    });
    QMetaObject::invokeMethod(
        worker,
        [worker] {
          worker->shutdown();
          QThread::currentThread()->quit();
        },
        Qt::QueuedConnection);
    static const bool registered = [] {
      qAddPostRoutine(waitForRetiringThreads);
      return true;
    }();
    Q_UNUSED(registered);
    retiringThreads().removeAll(nullptr);
    retiringThreads().append(thread);
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
  bool pairDevice(const QString& address) override { return queuePair(address, true); }
  bool unpairDevice(const QString& address) override { return queuePair(address, false); }
  void cancelPair() override { worker_->cancelPairing(); }

 private:
  // Pairing shares the connect flag: one of the two at a time, and no radio change meanwhile.
  bool queuePair(const QString& address, bool pair) {
    if (connectRunning_.exchange(true)) return false;
    QMetaObject::invokeMethod(
        worker_, [w = worker_, address, pair] { w->pairDevice(address, pair); },
        Qt::QueuedConnection);
    return true;
  }

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

  std::shared_ptr<BusyFlags> flags_ = std::make_shared<BusyFlags>();
  QThread* thread_ = nullptr;
  BluetoothWorker* worker_ = nullptr;
  std::atomic<bool>& connectRunning_ = flags_->connect;
  std::atomic<bool>& radioRunning_ = flags_->radio;
  bool started_ = false;
};

}  // namespace

std::unique_ptr<BluetoothBackend> createBluetoothBackend() {
  return std::make_unique<RealBluetoothBackend>();
}

bool hasBluetoothAdapter() {
  // BluetoothFindFirstRadio omits powered-off radios. Their adapter interfaces remain present,
  // so the service must use those to stay available when it starts with Bluetooth off.
  GUID interfaceId{};
  if (FAILED(CLSIDFromString(kAdapterInterface, &interfaceId))) return false;
  ULONG length = 0;
  return CM_Get_Device_Interface_List_SizeW(&length, &interfaceId, nullptr,
                                            CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS &&
         length > 1;
}

}  // namespace waku::bluetooth

#include "bluetooth_win.moc"
