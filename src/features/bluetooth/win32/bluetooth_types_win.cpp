// The device-type helpers of bluetooth.h (bluetooth_types.py). Pure functions, but the class
// and LE appearance constants come from the WinRT projection, hence win32/.
#include <winrt/Windows.Devices.Bluetooth.h>

#include <QStringList>
#include <algorithm>
#include <map>
#include <vector>

#include "features/bluetooth/bluetooth.h"

namespace waku::bluetooth {

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

QString pairNeedsSettingsMessage() {
  return QStringLiteral("This device needs pairing in Windows Settings");
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

namespace {

bool sameDevice(const BluetoothDevice& a, const BluetoothDevice& b) {
  if (a.isLe == b.isLe) return false;  // two classic (or two LE) entries are two devices
  if (a.name.isEmpty() || a.name == a.address) return false;
  if (a.name.compare(b.name, Qt::CaseInsensitive) != 0) return false;
  return a.deviceType == b.deviceType || a.deviceType == BtDeviceType::Generic ||
         b.deviceType == BtDeviceType::Generic;
}

BluetoothDevice mergeGroup(const std::vector<const BluetoothDevice*>& group) {
  const BluetoothDevice* base = group.front();
  for (const BluetoothDevice* device : group) {
    if (!device->isLe) {
      base = device;
      break;
    }
  }
  BluetoothDevice merged = *base;
  for (const BluetoothDevice* device : group) {
    if (device == base) continue;
    merged.otherAddresses << device->address;
    merged.otherAddresses << device->otherAddresses;
    merged.connected = merged.connected || device->connected;
    merged.paired = merged.paired || device->paired;
    merged.remembered = merged.remembered || device->remembered;
    if (merged.deviceType == BtDeviceType::Generic) {
      merged.deviceType = device->deviceType;
      if (merged.majorClass == 0) merged.majorClass = device->majorClass;
    }
    if (merged.containerId.isEmpty()) merged.containerId = device->containerId;
    if (merged.profiles.isEmpty()) merged.profiles = device->profiles;
    if (!merged.battery && device->battery) merged.battery = device->battery;
  }
  if (!merged.connected) merged.battery.reset();
  return merged;
}

}  // namespace

std::vector<BluetoothDevice> mergeSameDevices(const std::vector<BluetoothDevice>& devices) {
  std::vector<std::vector<const BluetoothDevice*>> groups;
  std::map<QString, size_t> byContainer;
  for (const BluetoothDevice& device : devices) {
    const QString container = device.containerId.trimmed().toLower();
    if (!container.isEmpty()) {
      const auto found = byContainer.find(container);
      if (found != byContainer.end()) {
        groups[found->second].push_back(&device);
      } else {
        byContainer[container] = groups.size();
        groups.push_back({&device});
      }
      continue;
    }
    bool placed = false;
    for (auto& group : groups) {
      const bool taken =
          std::any_of(group.begin(), group.end(),
                      [&](const BluetoothDevice* member) { return member->isLe == device.isLe; });
      if (!taken && sameDevice(*group.front(), device)) {
        group.push_back(&device);
        placed = true;
        break;
      }
    }
    if (!placed) groups.push_back({&device});
  }
  std::vector<BluetoothDevice> result;
  for (const auto& group : groups) result.push_back(mergeGroup(group));
  return result;
}
}  // namespace waku::bluetooth
