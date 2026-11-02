#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace waku::system_stats {

struct CpuSample {
  double percent = 0;
  std::vector<double> percentPerCore;
  double frequencyCurrent = 0;
  double frequencyMin = 0;
  double frequencyMax = 0;
  int coresPhysical = 1;
  int coresLogical = 1;
  std::optional<double> temperature;  // Celsius; nothing when no thermal zone reads
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

struct GpuSample {
  bool available = false;             // a GPU Engine counter exists
  double percent = 0;                 // busiest engine type over all adapters, 0..100
  std::uint64_t memoryUsed = 0;       // dedicated video memory in use, bytes
  std::uint64_t memoryTotal = 0;      // dedicated video memory of the hardware adapters, bytes
  std::string name;                   // name of the first hardware adapter
  std::optional<double> temperature;  // Celsius; nothing when the driver reports none
};

// One mounted volume with a drive letter.
struct DiskSample {
  std::string mount;  // "C:\"
  std::string name;   // volume label, may be empty
  std::string fileSystem;
  std::uint64_t total = 0;
  std::uint64_t free = 0;
  std::uint64_t used = 0;
  double percent = 0;      // used / total * 100
  bool system = false;     // holds the Windows folder
  bool removable = false;  // removable media (USB stick, card)

  bool operator==(const DiskSample&) const = default;
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

class GpuDataSource {
 public:
  virtual ~GpuDataSource() = default;
  virtual GpuSample sample() = 0;
};

class DiskDataSource {
 public:
  virtual ~DiskDataSource() = default;
  virtual std::vector<DiskSample> sample() = 0;
};

std::shared_ptr<CpuDataSource> createCpuDataSource();
std::shared_ptr<MemoryDataSource> createMemoryDataSource();
std::shared_ptr<BatteryDataSource> createBatteryDataSource();
std::shared_ptr<GpuDataSource> createGpuDataSource();
std::shared_ptr<DiskDataSource> createDiskDataSource();

}  // namespace waku::system_stats

#include <QMetaType>
Q_DECLARE_METATYPE(waku::system_stats::CpuSample)
Q_DECLARE_METATYPE(waku::system_stats::MemorySample)
Q_DECLARE_METATYPE(waku::system_stats::BatterySample)
Q_DECLARE_METATYPE(waku::system_stats::GpuSample)
Q_DECLARE_METATYPE(waku::system_stats::DiskSample)
