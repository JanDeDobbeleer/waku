#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <memory>

#include "kernel/qt/polled_source.h"

using wabar::kernel::PolledSource;

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
  QTest::qWait(150);
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
  QTest::qWait(250);
  EXPECT_EQ(source.counter->calls, 0);
}
