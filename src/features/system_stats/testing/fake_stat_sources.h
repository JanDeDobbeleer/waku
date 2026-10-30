#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <thread>

#include "features/system_stats/system_stats.h"

namespace wabar::system_stats::testing {

// Counts calls and concurrent calls; sample() runs on a QThreadPool thread.
template <class Base, class Sample>
class FakeSource : public Base {
 public:
  Sample sample() override {
    const int now = ++running;
    int seen = maxRunning.load();
    while (now > seen && !maxRunning.compare_exchange_weak(seen, now)) {
    }
    ++calls;
    if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    --running;
    if (fail) throw std::runtime_error("sample failed");
    return make();
  }
  virtual Sample make() = 0;

  std::atomic<int> calls{0};
  std::atomic<int> running{0};
  std::atomic<int> maxRunning{0};
  std::atomic<int> delayMs{0};
  std::atomic<bool> fail{false};
};

class FakeMemory final : public FakeSource<MemoryDataSource, MemorySample> {
 public:
  MemorySample make() override {
    MemorySample s;
    s.percent = percent;
    s.used = 4'000'000'000ULL;
    s.total = 8'000'000'000ULL;
    s.available = 4'000'000'000ULL;
    s.swapPercent = 7;
    return s;
  }
  std::atomic<double> percent{42};
};

class FakeCpu final : public FakeSource<CpuDataSource, CpuSample> {
 public:
  CpuSample make() override {
    CpuSample s;
    s.percent = 12.5;
    s.percentPerCore = {10, 15};
    s.frequencyCurrent = 3200;
    s.coresLogical = 2;
    if (hasTemperature) s.temperature = temperature.load();
    return s;
  }
  std::atomic<bool> hasTemperature{true};
  std::atomic<double> temperature{54.5};
};

class FakeGpu final : public FakeSource<GpuDataSource, GpuSample> {
 public:
  GpuSample make() override {
    GpuSample s;
    s.available = present;
    s.percent = 35;
    s.memoryUsed = 2'000'000'000ULL;
    s.memoryTotal = 8'000'000'000ULL;
    s.name = "Fake GPU";
    if (hasTemperature) s.temperature = 61.0;
    return s;
  }
  std::atomic<bool> hasTemperature{true};
  std::atomic<bool> present{true};
};

class FakeDisks final : public FakeSource<DiskDataSource, std::vector<DiskSample>> {
 public:
  std::vector<DiskSample> make() override {
    DiskSample system;
    system.mount = "C:\\";
    system.name = "Windows";
    system.fileSystem = "NTFS";
    system.total = 500'000'000'000ULL;
    system.free = 125'000'000'000ULL;
    system.used = 375'000'000'000ULL;
    system.percent = 75;
    system.system = true;
    DiskSample data;
    data.mount = "D:\\";
    data.total = 1'000'000'000'000ULL;
    data.free = 1'000'000'000'000ULL;
    return {system, data};
  }
};

class FakeBattery final : public FakeSource<BatteryDataSource, std::optional<BatterySample>> {
 public:
  std::optional<BatterySample> make() override {
    if (!present) return std::nullopt;
    BatterySample s;
    s.percent = 80;
    s.charging = true;
    s.plugged = true;
    s.timeRemaining = 90;
    return s;
  }
  std::atomic<bool> present{true};
};

}  // namespace wabar::system_stats::testing
