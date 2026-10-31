#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>

#include "features/system_stats/qml/stat_sources.h"
#include "features/system_stats/testing/fake_stat_sources.h"

using namespace wabar::system_stats;
using namespace wabar::system_stats::testing;

namespace {

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 3000) {
  return QTest::qWaitFor(condition, timeoutMs);
}

}  // namespace

TEST(StatSource, DoesNotSampleBeforeTheComponentIsComplete) {
  auto fake = std::make_shared<FakeMemory>();
  Memory memory(fake, nullptr);
  // No timer and no sample on the pool: nothing can run later either.
  EXPECT_FALSE(memory.polling());
  EXPECT_FALSE(memory.sampling());
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
  EXPECT_FALSE(memory.polling());
  EXPECT_FALSE(memory.sampling());
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
  EXPECT_FALSE(memory.polling());
  // A sample already in flight may still finish; after it nothing starts another.
  ASSERT_TRUE(waitFor([&] { return !memory.sampling(); }));
  const int calls = fake->calls.load();
  QCoreApplication::processEvents();
  EXPECT_FALSE(memory.polling());
  EXPECT_FALSE(memory.sampling());
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
  // The pool task holds the last other reference to the fake and drops it after it has queued the
  // apply; deliver that apply now: it must find the object gone.
  ASSERT_TRUE(waitFor([&] { return fake.use_count() == 1; }));
  QCoreApplication::sendPostedEvents();
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

TEST(StatSource, KeepsTheLastSamplesInTheHistoryRing) {
  auto fake = std::make_shared<FakeMemory>();
  Memory memory(fake, nullptr);
  memory.setHistorySize(3);
  memory.setInterval(100);
  memory.componentComplete();
  ASSERT_TRUE(waitFor([&] { return fake->calls.load() >= 5 && memory.history().size() == 3; }));
  fake->percent = 90;
  ASSERT_TRUE(waitFor([&] { return memory.history().last().toDouble() == 90; }));
  EXPECT_EQ(memory.history().size(), 3);  // the oldest samples dropped out
  memory.setHistorySize(2);
  EXPECT_EQ(memory.history().size(), 2);
  EXPECT_EQ(memory.history().last().toDouble(), 90);
}

TEST(StatSource, HistoryDefaultsTo60AndStartsEmpty) {
  Memory memory(std::make_shared<FakeMemory>(), nullptr);
  EXPECT_EQ(memory.historySize(), 60);
  EXPECT_TRUE(memory.history().isEmpty());
}

TEST(CpuSource, HistoryHoldsTheLoadAndExposesCoresAndMaxFrequency) {
  Cpu cpu(std::make_shared<FakeCpu>(), nullptr);
  cpu.componentComplete();
  ASSERT_TRUE(waitFor([&] { return cpu.ready(); }));
  ASSERT_FALSE(cpu.history().isEmpty());
  EXPECT_DOUBLE_EQ(cpu.history().first().toDouble(), 12.5);
  EXPECT_EQ(cpu.coresPhysical(), 1);
  EXPECT_DOUBLE_EQ(cpu.maxFrequency(), 0);
}
