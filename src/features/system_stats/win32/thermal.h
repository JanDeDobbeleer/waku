#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "features/system_stats/temperature.h"

namespace wabar::system_stats {

// Reads the PDH set "Thermal Zone Information" (documented, no admin). Not thread safe; the owner
// serialises calls. Opens its query on the first call and gives up for good when the set is
// missing, so a machine without ACPI thermal zones costs one failed open.
class PdhThermalReader {
 public:
  PdhThermalReader();
  ~PdhThermalReader();
  PdhThermalReader(const PdhThermalReader&) = delete;
  PdhThermalReader& operator=(const PdhThermalReader&) = delete;

  // Every zone that has an instance, with the raw counter values. Empty when unavailable.
  std::vector<ThermalZone> zones();

  // selectCpuTemperature(zones()). Nothing unless kernel::liveDesktop().
  std::optional<double> cpuTemperature();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace wabar::system_stats
