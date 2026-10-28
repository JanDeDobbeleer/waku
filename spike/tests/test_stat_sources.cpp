#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

#include "spike/services/system_stats.h"

using namespace tidy::services;
namespace platform = yasb::platform;

namespace {

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

class FakeMemory final : public FakeSource<platform::MemoryDataSource, platform::MemorySample> {
 public:
  platform::MemorySample make() override {
    platform::MemorySample s;
    s.percent = percent;
    s.used = 4'000'000'000ULL;
    s.total = 8'000'000'000ULL;
    s.available = 4'000'000'000ULL;
    s.swapPercent = 7;
    return s;
  }
  std::atomic<double> percent{42};
};

class FakeCpu final : public FakeSource<platform::CpuDataSource, platform::CpuSample> {
 public:
  platform::CpuSample make() override {
    platform::CpuSample s;
    s.percent = 12.5;
    s.percentPerCore = {10, 15};
    s.frequencyCurrent = 3200;
    s.coresLogical = 2;
    return s;
  }
};

class FakeBattery final
    : public FakeSource<platform::BatteryDataSource, std::optional<platform::BatterySample>> {
 public:
  std::optional<platform::BatterySample> make() override {
    if (!present) return std::nullopt;
    platform::BatterySample s;
    s.percent = 80;
    s.charging = true;
    s.plugged = true;
    s.timeRemaining = 90;
    return s;
  }
  std::atomic<bool> present{true};
};

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 3000) {
  return QTest::qWaitFor(condition, timeoutMs);
}

}  // namespace

TEST(StatSource, DoesNotSampleBeforeTheComponentIsComplete) {
  auto fake = std::make_shared<FakeMemory>();
  Memory memory(fake, nullptr);
  QTest::qWait(300);
  EXPECT_EQ(fake->calls.load(), 0);
  EXPECT_FALSE(memory.ready());
}

TEST(StatSource, AppliesTheFirstSampleOnTheGuiThread) {
  auto fake = std::make_shared<FakeMemory>();
  Memory memory(fake, nullptr);
  QSignalSpy updated(&memory, &Memory::updated);
  QSignalSpy readyChanged(&memory, &Memory::readyChanged);
  memory.componentComplete();
  ASSERT_TRUE(waitFor([&] { return memory.ready(); }));
  EXPECT_DOUBLE_EQ(memory.percent(), 42);
  EXPECT_EQ(memory.used(), 4'000'000'000LL);
  EXPECT_EQ(memory.total(), 8'000'000'000LL);
  EXPECT_EQ(memory.available(), 4'000'000'000LL);
  EXPECT_DOUBLE_EQ(memory.swapPercent(), 7);
  EXPECT_GE(updated.count(), 1);
  EXPECT_EQ(readyChanged.count(), 1);
}

TEST(StatSource, PollsAtTheInterval) {
  auto fake = std::make_shared<FakeMemory>();
  Memory memory(fake, nullptr);
  memory.setInterval(100);
  memory.componentComplete();
  ASSERT_TRUE(waitFor([&] { return fake->calls.load() >= 4; }));
  fake->percent = 77;
  ASSERT_TRUE(waitFor([&] { return memory.percent() == 77; }));
}

TEST(StatSource, IntervalHasAFloorOf100Ms) {
  Memory memory(std::make_shared<FakeMemory>(), nullptr);
  memory.setInterval(1);
  EXPECT_EQ(memory.interval(), 100);
}

TEST(StatSource, InactiveSourceDoesNotPollUntilActivated) {
  auto fake = std::make_shared<FakeMemory>();
  Memory memory(fake, nullptr);
  memory.setActive(false);
  memory.componentComplete();
  QTest::qWait(300);
  EXPECT_EQ(fake->calls.load(), 0);
  EXPECT_FALSE(memory.ready());

  memory.setActive(true);
  ASSERT_TRUE(waitFor([&] { return memory.ready(); }));
}

TEST(StatSource, StopsPollingWhenDeactivated) {
  auto fake = std::make_shared<FakeMemory>();
  Memory memory(fake, nullptr);
  memory.setInterval(100);
  memory.componentComplete();
  ASSERT_TRUE(waitFor([&] { return memory.ready(); }));
  memory.setActive(false);
  QTest::qWait(150);  // a sample already in flight may still finish
  const int calls = fake->calls.load();
  QTest::qWait(400);
  EXPECT_EQ(fake->calls.load(), calls);
}

TEST(StatSource, FailingSampleDoesNotWedgeTheSource) {
  auto fake = std::make_shared<FakeMemory>();
  fake->fail = true;
  Memory memory(fake, nullptr);
  memory.setInterval(100);
  memory.componentComplete();
  ASSERT_TRUE(waitFor([&] { return fake->calls.load() >= 3; }));
  EXPECT_FALSE(memory.ready());  // no sample was ever applied

  fake->fail = false;
  ASSERT_TRUE(waitFor([&] { return memory.ready(); }));
  EXPECT_DOUBLE_EQ(memory.percent(), 42);
}

TEST(StatSource, NeverRunsTwoSamplesAtOnce) {
  auto fake = std::make_shared<FakeMemory>();
  fake->delayMs = 250;
  Memory memory(fake, nullptr);
  memory.setInterval(100);
  memory.componentComplete();
  ASSERT_TRUE(waitFor([&] { return fake->calls.load() >= 3; }, 5000));
  EXPECT_EQ(fake->maxRunning.load(), 1);
}

TEST(StatSource, ResultOfADestroyedSourceIsDropped) {
  auto fake = std::make_shared<FakeMemory>();
  fake->delayMs = 200;
  {
    auto memory = std::make_unique<Memory>(fake, nullptr);
    memory->componentComplete();
    ASSERT_TRUE(waitFor([&] { return fake->calls.load() >= 1; }));
  }  // destroyed while the pool thread is still inside sample()
  QTest::qWait(500);  // the queued apply must find the object gone
  SUCCEED();
}

TEST(CpuSource, ExposesPercentPerCoreFrequencyAndCores) {
  Cpu cpu(std::make_shared<FakeCpu>(), nullptr);
  cpu.componentComplete();
  ASSERT_TRUE(waitFor([&] { return cpu.ready(); }));
  EXPECT_DOUBLE_EQ(cpu.percent(), 12.5);
  EXPECT_DOUBLE_EQ(cpu.frequency(), 3200);
  EXPECT_EQ(cpu.cores(), 2);
  const QVariantList perCore = cpu.perCore();
  ASSERT_EQ(perCore.size(), 2);
  EXPECT_DOUBLE_EQ(perCore[0].toDouble(), 10);
  EXPECT_DOUBLE_EQ(perCore[1].toDouble(), 15);
}

TEST(BatterySource, ExposesTheSample) {
  Battery battery(std::make_shared<FakeBattery>(), nullptr);
  battery.componentComplete();
  ASSERT_TRUE(waitFor([&] { return battery.ready(); }));
  EXPECT_TRUE(battery.available());
  EXPECT_EQ(battery.percent(), 80);
  EXPECT_TRUE(battery.charging());
  EXPECT_TRUE(battery.plugged());
  EXPECT_EQ(battery.timeRemaining(), 90);
}

TEST(BatterySource, ReportsUnavailableWithoutABattery) {
  auto fake = std::make_shared<FakeBattery>();
  fake->present = false;
  Battery battery(fake, nullptr);
  battery.componentComplete();
  ASSERT_TRUE(waitFor([&] { return battery.ready(); }));  // a sample arrived: there is just none
  EXPECT_FALSE(battery.available());
}
