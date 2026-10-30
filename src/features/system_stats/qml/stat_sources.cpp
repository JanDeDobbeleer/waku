#include "features/system_stats/qml/stat_sources.h"

#include <algorithm>

namespace wabar::system_stats {

Memory::Memory(QObject* parent) : Memory(createMemoryDataSource(), parent) {}

Memory::Memory(std::shared_ptr<MemoryDataSource> source, QObject* parent)
    : kernel::StatSource(parent), source_(std::move(source)) {}

void Memory::sample() {
  sampleAsync(source_, [this](MemorySample s) {
    sample_ = s;
    recordHistory(s.percent);
  });
}

Cpu::Cpu(QObject* parent) : Cpu(createCpuDataSource(), parent) {}

Cpu::Cpu(std::shared_ptr<CpuDataSource> source, QObject* parent)
    : kernel::StatSource(parent), source_(std::move(source)) {}

QVariantList Cpu::perCore() const {
  QVariantList list;
  for (double v : sample_.percentPerCore) list << v;
  return list;
}

void Cpu::sample() {
  sampleAsync(source_, [this](CpuSample s) {
    sample_ = std::move(s);
    recordHistory(sample_.percent);
  });
}

Battery::Battery(QObject* parent) : Battery(createBatteryDataSource(), parent) {}

Battery::Battery(std::shared_ptr<BatteryDataSource> source, QObject* parent)
    : kernel::StatSource(parent), source_(std::move(source)) {}

void Battery::sample() {
  sampleAsync(source_, [this](std::optional<BatterySample> s) {
    available_ = s.has_value();
    if (s) sample_ = std::move(*s);
  });
}

Gpu::Gpu(QObject* parent) : Gpu(createGpuDataSource(), parent) {}

Gpu::Gpu(std::shared_ptr<GpuDataSource> source, QObject* parent)
    : kernel::StatSource(parent), source_(std::move(source)) {}

double Gpu::memoryPercent() const {
  if (sample_.memoryTotal == 0) return 0;
  return std::min(100.0, 100.0 * static_cast<double>(sample_.memoryUsed) /
                             static_cast<double>(sample_.memoryTotal));
}

void Gpu::sample() {
  sampleAsync(source_, [this](GpuSample s) {
    sample_ = std::move(s);
    recordHistory(sample_.percent);
  });
}

Disks::Disks(QObject* parent) : Disks(createDiskDataSource(), parent) {}

Disks::Disks(std::shared_ptr<DiskDataSource> source, QObject* parent)
    : kernel::StatSource(parent), source_(std::move(source)) {}

void Disks::sample() {
  sampleAsync(source_, [this](std::vector<DiskSample> samples) {
    disks_.clear();
    primary_.clear();
    for (const DiskSample& disk : samples) {
      QVariantMap map{{QStringLiteral("mount"), QString::fromStdString(disk.mount)},
                      {QStringLiteral("name"), QString::fromStdString(disk.name)},
                      {QStringLiteral("fileSystem"), QString::fromStdString(disk.fileSystem)},
                      {QStringLiteral("total"), static_cast<qlonglong>(disk.total)},
                      {QStringLiteral("free"), static_cast<qlonglong>(disk.free)},
                      {QStringLiteral("used"), static_cast<qlonglong>(disk.used)},
                      {QStringLiteral("perc"), disk.percent},
                      {QStringLiteral("percent"), disk.percent},
                      {QStringLiteral("hasRoot"), disk.system},
                      {QStringLiteral("system"), disk.system},
                      {QStringLiteral("removable"), disk.removable}};
      if (disk.system) primary_ = map;
      disks_.append(map);
    }
  });
}

}  // namespace wabar::system_stats
