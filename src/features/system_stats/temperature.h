#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wabar::system_stats {

// One ACPI thermal zone from the PDH set "Thermal Zone Information". The name is the ACPI path
// ("\_SB.TZ0"); it does not say which part the zone measures. A reading of 0 means the counter
// gave no value.
struct ThermalZone {
  std::string name;
  double deciKelvin = 0;  // "High Precision Temperature", tenths of a Kelvin
  double kelvin = 0;      // "Temperature", whole Kelvin
};

// Celsius from tenths of a Kelvin, rounded to 0.01 (a tenth of a Kelvin is 0.05 C off the 0.1
// grid). Nothing for a reading that is not a plausible temperature (above 0 C and up to 150 C):
// ACPI zones report 0 K or placeholder values such as -40 C when they have no sensor.
std::optional<double> celsiusFromDeciKelvin(double deciKelvin);

// Celsius of one zone: the high precision counter when it reads, else the whole-Kelvin counter.
std::optional<double> zoneCelsius(const ThermalZone& zone);

// The CPU temperature: the hottest zone with a plausible reading. ACPI zone names carry no part
// label, so no zone can be tied to the CPU by name; the hottest zone is the SoC/package proxy.
// Nothing when no zone reads.
std::optional<double> selectCpuTemperature(const std::vector<ThermalZone>& zones);

// Celsius from D3DKMT_ADAPTER_PERFDATA::Temperature (deci-Celsius). 0 means the driver reports no
// temperature, so it gives nothing, like an out-of-range value.
std::optional<double> celsiusFromDeciCelsius(std::uint32_t deciCelsius);

}  // namespace wabar::system_stats
