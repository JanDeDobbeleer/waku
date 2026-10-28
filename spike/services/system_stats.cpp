#include "spike/services/system_stats.h"

namespace yasb::services {

Memory::Memory(QObject* parent) : StatSource(parent), source_(platform::createMemoryDataSource()) {}

void Memory::sample() {
  sampleAsync(source_, [this](platform::MemorySample s) { sample_ = s; });
}

Cpu::Cpu(QObject* parent) : StatSource(parent), source_(platform::createCpuDataSource()) {}

QVariantList Cpu::perCore() const {
  QVariantList list;
  for (double v : sample_.percentPerCore) list << v;
  return list;
}

void Cpu::sample() {
  sampleAsync(source_, [this](platform::CpuSample s) { sample_ = std::move(s); });
}

Battery::Battery(QObject* parent)
    : StatSource(parent), source_(platform::createBatteryDataSource()) {}

void Battery::sample() {
  sampleAsync(source_, [this](std::optional<platform::BatterySample> s) {
    available_ = s.has_value();
    if (s) sample_ = std::move(*s);
  });
}

}  // namespace yasb::services
