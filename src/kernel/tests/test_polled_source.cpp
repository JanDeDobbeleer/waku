#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <memory>

#include "kernel/qt/polled_source.h"

using waku::kernel::PolledSource;

namespace {

struct Counter {
  int sample() { return ++calls; }
  std::atomic<int> calls{0};
};

class CountingSource final : public PolledSource {
 public:
  std::shared_ptr<Counter> counter = std::make_shared<Counter>();
  int applied = 0;

 protected:
  void sample() override {
    sampleAsync(counter, [this](int) { ++applied; });
  }
};

}  // namespace

TEST(KernelPolledSource, DoesNothingBeforeBegin) {
  CountingSource source;
  // No timer and no sample on the pool: nothing can run later either.
  EXPECT_FALSE(source.polling());
  EXPECT_FALSE(source.sampling());
  EXPECT_EQ(source.counter->calls, 0);
  EXPECT_FALSE(source.ready());
}

TEST(KernelPolledSource, BeginSamplesAtOnceAndAppliesOnTheGuiThread) {
  CountingSource source;
  QSignalSpy updated(&source, &PolledSource::updated);
  source.begin();
  ASSERT_TRUE(updated.wait(2000));
  EXPECT_TRUE(source.ready());
  EXPECT_GE(source.applied, 1);
}

TEST(KernelPolledSource, IntervalHasAFloorOf100Ms) {
  CountingSource source;
  source.setInterval(1);
  EXPECT_EQ(source.interval(), 100);
}

TEST(KernelPolledSource, InactiveSourceDoesNotPoll) {
  CountingSource source;
  source.setActive(false);
  source.begin();
  EXPECT_FALSE(source.polling());
  EXPECT_FALSE(source.sampling());
  EXPECT_EQ(source.counter->calls, 0);
}

TEST(KernelPolledSource, BeginPollsAndDeactivatingStopsTheTimer) {
  CountingSource source;
  source.begin();
  EXPECT_TRUE(source.polling());
  EXPECT_TRUE(source.sampling());  // the first sample starts at once
  ASSERT_TRUE(QTest::qWaitFor([&] { return !source.sampling(); }, 5000));
  source.setActive(false);
  EXPECT_FALSE(source.polling());
}
