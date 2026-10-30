#include "features/system_stats/gpu_engines.h"

#include <algorithm>
#include <map>

namespace wabar::system_stats {

std::string gpuEngineKey(const std::string& instance) {
  const size_t luid = instance.find("luid_");
  const size_t phys = instance.find("_phys_", luid == std::string::npos ? 0 : luid);
  const size_t type = instance.find("engtype_");
  if (luid == std::string::npos || phys == std::string::npos || type == std::string::npos ||
      phys < luid) {
    return {};
  }
  return instance.substr(luid, phys - luid) + "|" + instance.substr(type + 8);
}

double gpuUsage(const std::vector<GpuEngineValue>& values) {
  std::map<std::string, double> perEngine;
  for (const GpuEngineValue& value : values) {
    const std::string key = gpuEngineKey(value.instance);
    if (!key.empty() && value.percent > 0) perEngine[key] += value.percent;
  }
  double busiest = 0;
  for (const auto& [key, sum] : perEngine) busiest = std::max(busiest, sum);
  return std::clamp(busiest, 0.0, 100.0);
}

}  // namespace wabar::system_stats
