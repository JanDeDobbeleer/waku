#pragma once

#include <string>
#include <vector>

namespace wabar::system_stats {

// One instance of the PDH counter "\GPU Engine(*)\Utilization Percentage". The instance name looks
// like "pid_1234_luid_0x00000000_0x0000A1B2_phys_0_eng_3_engtype_3D".
struct GpuEngineValue {
  std::string instance;
  double percent = 0;
};

// The adapter and the engine type of an instance name, joined as "<luid>|<engtype>"; empty when the
// name has no "luid_" and "engtype_" parts.
std::string gpuEngineKey(const std::string& instance);

// GPU load the way Task Manager shows it per engine: the utilization of every process on one
// engine type of one adapter adds up, and the busiest engine type wins. Clamped to 0..100.
double gpuUsage(const std::vector<GpuEngineValue>& values);

}  // namespace wabar::system_stats
