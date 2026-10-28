#include "spike/services/system_stats.h"

namespace yasb::services {

Memory::Memory(QObject* parent) : Memory(platform::createMemoryDataSource(), parent) {}

Memory::Memory(std::shared_ptr<platform::MemoryDataSource> source, QObject* parent)
    : StatSource(parent), source_(std::move(source)) {}

void Memory::sample() {
  sampleAsync(source_, [this](platform::MemorySample s) { sample_ = s; });
}

Cpu::Cpu(QObject* parent) : Cpu(platform::createCpuDataSource(), parent) {}

Cpu::Cpu(std::shared_ptr<platform::CpuDataSource> source, QObject* parent)
    : StatSource(parent), source_(std::move(source)) {}

QVariantList Cpu::perCore() const {
  QVariantList list;
  for (double v : sample_.percentPerCore) list << v;
  return list;
}

void Cpu::sample() {
  sampleAsync(source_, [this](platform::CpuSample s) { sample_ = std::move(s); });
}

Battery::Battery(QObject* parent) : Battery(platform::createBatteryDataSource(), parent) {}

Battery::Battery(std::shared_ptr<platform::BatteryDataSource> source, QObject* parent)
    : StatSource(parent), source_(std::move(source)) {}

void Battery::sample() {
  sampleAsync(source_, [this](std::optional<platform::BatterySample> s) {
    available_ = s.has_value();
    if (s) sample_ = std::move(*s);
  });
}

}  // namespace yasb::services
