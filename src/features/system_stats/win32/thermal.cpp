#include "features/system_stats/win32/thermal.h"

#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <QString>
#include <map>

#include "kernel/qt/live_desktop.h"

namespace waku::system_stats {
namespace {

// Documented APIs only: the PDH counters "\Thermal Zone Information(*)\High Precision Temperature"
// (tenths of a Kelvin) and "\Thermal Zone Information(*)\Temperature" (Kelvin).

// Raw values by instance name; instances without valid data are left out.
std::map<std::string, double> readArray(PDH_HCOUNTER counter) {
  std::map<std::string, double> values;
  if (!counter) return values;
  DWORD bytes = 0;
  DWORD count = 0;
  if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) !=
      PDH_MORE_DATA) {
    return values;
  }
  std::vector<std::byte> buffer(bytes);
  auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
  if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items) !=
      ERROR_SUCCESS) {
    return values;
  }
  for (DWORD i = 0; i < count; ++i) {
    const DWORD status = items[i].FmtValue.CStatus;
    if (status != PDH_CSTATUS_VALID_DATA && status != PDH_CSTATUS_NEW_DATA) continue;
    values[QString::fromWCharArray(items[i].szName).toStdString()] = items[i].FmtValue.doubleValue;
  }
  return values;
}

}  // namespace

struct PdhThermalReader::Impl {
  PDH_HQUERY query = nullptr;
  PDH_HCOUNTER precise = nullptr;
  PDH_HCOUNTER whole = nullptr;
  bool failed = false;

  ~Impl() {
    if (query) PdhCloseQuery(query);
  }

  bool open() {
    if (query) return true;
    if (failed) return false;
    if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) {
      query = nullptr;
      failed = true;
      return false;
    }
    PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\High Precision Temperature", 0,
                          &precise);
    PdhAddEnglishCounterW(query, L"\\Thermal Zone Information(*)\\Temperature", 0, &whole);
    if (!precise && !whole) {
      PdhCloseQuery(query);
      query = nullptr;
      failed = true;
      return false;
    }
    PdhCollectQueryData(query);
    return true;
  }
};

PdhThermalReader::PdhThermalReader() : impl_(std::make_unique<Impl>()) {}
PdhThermalReader::~PdhThermalReader() = default;

std::vector<ThermalZone> PdhThermalReader::zones() {
  std::vector<ThermalZone> result;
  if (!impl_->open()) return result;
  if (PdhCollectQueryData(impl_->query) != ERROR_SUCCESS) return result;
  std::map<std::string, ThermalZone> byName;
  for (const auto& [name, value] : readArray(impl_->precise)) {
    byName[name].name = name;
    byName[name].deciKelvin = value;
  }
  for (const auto& [name, value] : readArray(impl_->whole)) {
    byName[name].name = name;
    byName[name].kelvin = value;
  }
  for (auto& entry : byName) result.push_back(std::move(entry.second));
  return result;
}

std::optional<double> PdhThermalReader::cpuTemperature() {
  if (!kernel::liveDesktop(QStringLiteral("system_stats"))) return std::nullopt;
  return selectCpuTemperature(zones());
}

}  // namespace waku::system_stats
