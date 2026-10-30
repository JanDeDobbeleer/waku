#include "features/system_stats/temperature.h"

#include <algorithm>
#include <cmath>

namespace wabar::system_stats {
namespace {

constexpr double kMaxCelsius = 150.0;

double roundTwo(double value) { return std::nearbyint(value * 100.0) / 100.0; }

std::optional<double> plausible(double celsius) {
  const double rounded = roundTwo(celsius);
  if (!std::isfinite(rounded) || rounded <= 0.0 || rounded > kMaxCelsius) return std::nullopt;
  return rounded;
}

}  // namespace

std::optional<double> celsiusFromDeciKelvin(double deciKelvin) {
  if (!(deciKelvin > 0)) return std::nullopt;
  return plausible(deciKelvin / 10.0 - 273.15);
}

std::optional<double> zoneCelsius(const ThermalZone& zone) {
  if (auto precise = celsiusFromDeciKelvin(zone.deciKelvin)) return precise;
  if (!(zone.kelvin > 0)) return std::nullopt;
  return celsiusFromDeciKelvin(zone.kelvin * 10.0);
}

std::optional<double> selectCpuTemperature(const std::vector<ThermalZone>& zones) {
  std::optional<double> best;
  for (const ThermalZone& zone : zones) {
    const std::optional<double> celsius = zoneCelsius(zone);
    if (celsius && (!best || *celsius > *best)) best = celsius;
  }
  return best;
}

std::optional<double> celsiusFromDeciCelsius(std::uint32_t deciCelsius) {
  if (deciCelsius == 0) return std::nullopt;
  return plausible(static_cast<double>(deciCelsius) / 10.0);
}

}  // namespace wabar::system_stats
