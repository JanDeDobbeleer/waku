#include "features/system_stats/qml/stat_sources.h"

namespace wabar::system_stats {

Memory::Memory(QObject* parent) : Memory(createMemoryDataSource(), parent) {}

Memory::Memory(std::shared_ptr<MemoryDataSource> source, QObject* parent)
    : StatSource(parent), source_(std::move(source)) {}

void Memory::sample() {
  sampleAsync(source_, [this](MemorySample s) {
    sample_ = s;
    recordHistory(s.percent);
  });
}

Cpu::Cpu(QObject* parent) : Cpu(createCpuDataSource(), parent) {}

Cpu::Cpu(std::shared_ptr<CpuDataSource> source, QObject* parent)
    : StatSource(parent), source_(std::move(source)) {}

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
    : StatSource(parent), source_(std::move(source)) {}

void Battery::sample() {
  sampleAsync(source_, [this](std::optional<BatterySample> s) {
    available_ = s.has_value();
    if (s) sample_ = std::move(*s);
  });
}

}  // namespace wabar::system_stats
