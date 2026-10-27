#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yasb::platform {

struct CpuSample {
  double percent = 0;
  std::vector<double> percentPerCore;
  double frequencyCurrent = 0;
  double frequencyMin = 0;
  double frequencyMax = 0;
  int coresPhysical = 1;
  int coresLogical = 1;
};

struct MemorySample {
  std::uint64_t total = 0;
  std::uint64_t available = 0;
  double percent = 0;
  std::uint64_t used = 0;
  std::uint64_t free = 0;
  std::uint64_t swapTotal = 0;
  std::uint64_t swapUsed = 0;
  std::uint64_t swapFree = 0;
  double swapPercent = 0;
  std::uint64_t cached = 0;
};

struct BatterySample {
  int percent = 0;
  bool charging = false;
  bool plugged = false;
  int timeRemaining = -1;
  std::optional<double> rate;
  std::optional<double> voltage;
  std::optional<std::uint32_t> capacity;
  std::optional<std::uint32_t> fullCapacity;
  std::optional<std::uint32_t> designedCapacity;
  std::optional<double> temperature;
  std::optional<std::uint32_t> cycleCount;
  std::optional<std::string> chemistry;
  std::optional<double> healthPercent;
};

class CpuDataSource {
 public:
  virtual ~CpuDataSource() = default;
  virtual CpuSample sample() = 0;
};

class MemoryDataSource {
 public:
  virtual ~MemoryDataSource() = default;
  virtual MemorySample sample() = 0;
};

class BatteryDataSource {
 public:
  virtual ~BatteryDataSource() = default;
  virtual std::optional<BatterySample> sample() = 0;
};

std::shared_ptr<CpuDataSource> createCpuDataSource();
std::shared_ptr<MemoryDataSource> createMemoryDataSource();
std::shared_ptr<BatteryDataSource> createBatteryDataSource();

}  // namespace yasb::platform

#include <QMetaType>
Q_DECLARE_METATYPE(yasb::platform::CpuSample)
Q_DECLARE_METATYPE(yasb::platform::MemorySample)
Q_DECLARE_METATYPE(yasb::platform::BatterySample)
