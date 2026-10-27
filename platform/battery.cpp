#include "platform/battery.h"

#include <windows.h>

// These SDK headers depend on the Windows declarations above.
#include <batclass.h>
#include <devguid.h>
#include <setupapi.h>
#include <winioctl.h>

#include <cmath>
#include <vector>

namespace yasb::platform {
namespace {

constexpr int kPowerTimeUnknown = -1;
constexpr int kPowerTimeUnlimited = -2;

double roundOne(double value) { return std::nearbyint(value * 10.0) / 10.0; }

bool queryValue(HANDLE handle, ULONG tag, BATTERY_QUERY_INFORMATION_LEVEL level, void* output,
                DWORD size) {
  BATTERY_QUERY_INFORMATION query{};
  query.BatteryTag = tag;
  query.InformationLevel = level;
  DWORD returned = 0;
  return DeviceIoControl(handle, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), output,
                         size, &returned, nullptr) != FALSE;
}

std::optional<std::wstring> firstBatteryPath() {
  HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_BATTERY, nullptr, nullptr,
                                          DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
  if (devices == INVALID_HANDLE_VALUE) return std::nullopt;
  std::optional<std::wstring> path;
  SP_DEVICE_INTERFACE_DATA iface{};
  iface.cbSize = sizeof(iface);
  if (SetupDiEnumDeviceInterfaces(devices, nullptr, &GUID_DEVCLASS_BATTERY, 0, &iface)) {
    DWORD required = 0;
    SetupDiGetDeviceInterfaceDetailW(devices, &iface, nullptr, 0, &required, nullptr);
    if (required >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
      std::vector<std::byte> buffer(required);
      auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
      detail->cbSize = sizeof(*detail);
      if (SetupDiGetDeviceInterfaceDetailW(devices, &iface, detail, required, nullptr, nullptr))
        path = detail->DevicePath;
    }
  }
  SetupDiDestroyDeviceInfoList(devices);
  return path;
}

}  // namespace

std::optional<BatterySample> WindowsBatteryDataSource::sample() {
  SYSTEM_POWER_STATUS power{};
  if (!GetSystemPowerStatus(&power) || (power.BatteryFlag & 0x80) ||
      power.BatteryLifePercent == 255) {
    return std::nullopt;
  }

  BatterySample out;
  out.percent = power.BatteryLifePercent;
  out.plugged = power.ACLineStatus == 1;
  out.charging = (power.BatteryFlag & 8) != 0;
  out.timeRemaining = power.BatteryLifeTime == 0xFFFFFFFF
                          ? (out.plugged ? kPowerTimeUnlimited : kPowerTimeUnknown)
                          : static_cast<int>(power.BatteryLifeTime);

  if (devicePath_.empty()) {
    if (auto path = firstBatteryPath()) devicePath_ = *path;
  }
  if (devicePath_.empty()) return out;
  HANDLE battery = CreateFileW(devicePath_.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
  if (battery == INVALID_HANDLE_VALUE) return out;
  struct HandleGuard {
    HANDLE value;
    ~HandleGuard() { CloseHandle(value); }
  } close{battery};

  ULONG timeout = 0;
  ULONG tag = 0;
  DWORD returned = 0;
  if (!DeviceIoControl(battery, IOCTL_BATTERY_QUERY_TAG, &timeout, sizeof(timeout), &tag,
                       sizeof(tag), &returned, nullptr) ||
      tag == 0) {
    return out;
  }

  BATTERY_INFORMATION info{};
  if (queryValue(battery, tag, BatteryInformation, &info, sizeof(info))) {
    if (info.DesignedCapacity != BATTERY_UNKNOWN_CAPACITY)
      out.designedCapacity = info.DesignedCapacity;
    if (info.FullChargedCapacity != BATTERY_UNKNOWN_CAPACITY)
      out.fullCapacity = info.FullChargedCapacity;
    if (info.CycleCount > 0) out.cycleCount = info.CycleCount;
    std::string chemistry(reinterpret_cast<const char*>(info.Chemistry), sizeof(info.Chemistry));
    const auto nul = chemistry.find('\0');
    if (nul != std::string::npos) chemistry.resize(nul);
    if (!chemistry.empty()) out.chemistry = chemistry;
  }

  BATTERY_WAIT_STATUS wait{};
  wait.BatteryTag = tag;
  wait.HighCapacity = BATTERY_UNKNOWN_CAPACITY;
  BATTERY_STATUS state{};
  if (DeviceIoControl(battery, IOCTL_BATTERY_QUERY_STATUS, &wait, sizeof(wait), &state,
                      sizeof(state), &returned, nullptr)) {
    out.charging = (state.PowerState & BATTERY_CHARGING) != 0;
    if (state.Rate != static_cast<LONG>(BATTERY_UNKNOWN_RATE))
      out.rate = static_cast<double>(state.Rate) / 1000.0;
    if (state.Voltage != BATTERY_UNKNOWN_VOLTAGE) out.voltage = state.Voltage / 1000.0;
    if (state.Capacity != BATTERY_UNKNOWN_CAPACITY) out.capacity = state.Capacity;
  }

  ULONG temperature = 0;
  if (queryValue(battery, tag, BatteryTemperature, &temperature, sizeof(temperature)) &&
      temperature > 0) {
    const double celsius = static_cast<double>(temperature) / 10.0 - 273.15;
    if (celsius >= -40 && celsius <= 100) out.temperature = roundOne(celsius);
  }
  if (out.fullCapacity && out.designedCapacity && *out.designedCapacity > 0) {
    out.healthPercent =
        roundOne(static_cast<double>(*out.fullCapacity) / *out.designedCapacity * 100);
  }
  return out;
}

std::shared_ptr<BatteryDataSource> createBatteryDataSource() {
  return std::make_shared<WindowsBatteryDataSource>();
}

}  // namespace yasb::platform
