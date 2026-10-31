#include <gtest/gtest.h>

#include <QTest>
#include <memory>

#include "kernel/qt/linger.h"

using wabar::kernel::linger;
using wabar::kernel::LingeringHub;

namespace {

struct Counted {
  explicit Counted(int* alive) : alive_(alive) { ++*alive_; }
  ~Counted() { --*alive_; }
  int* alive_;
};

}  // namespace

TEST(Linger, KeepsTheObjectUntilTheTimeIsUp) {
  int alive = 0;
  linger(std::make_shared<Counted>(&alive), 100);
  EXPECT_EQ(alive, 1);
  EXPECT_TRUE(QTest::qWaitFor([&] { return alive == 0; }, 3000));
}

TEST(Linger, NoTimeReleasesAtOnce) {
  int alive = 0;
  linger(std::make_shared<Counted>(&alive), 0);
  EXPECT_EQ(alive, 0);
}

TEST(LingeringHub, HoldersShareOneObject) {
  int alive = 0;
  int made = 0;
  LingeringHub<Counted> hub(100);
  const auto make = [&] {
    ++made;
    return std::make_shared<Counted>(&alive);
  };
  auto a = hub.acquire(make);
  auto b = hub.acquire(make);
  EXPECT_EQ(a.get(), b.get());
  EXPECT_EQ(made, 1);
  EXPECT_EQ(alive, 1);
}

TEST(LingeringHub, AHolderWithinTheGracePeriodGetsTheSameObject) {
  int alive = 0;
  int made = 0;
  LingeringHub<Counted> hub(2000);
  const auto make = [&] {
    ++made;
    return std::make_shared<Counted>(&alive);
  };
  Counted* first = nullptr;
  {
    auto a = hub.acquire(make);
    first = a.get();
  }
  // The last holder is gone (as during a reload), the object is not.
  EXPECT_EQ(alive, 1);
  auto b = hub.acquire(make);
  EXPECT_EQ(b.get(), first);
  EXPECT_EQ(made, 1);
}

TEST(LingeringHub, TheObjectGoesWhenNobodyComesBack) {
  int alive = 0;
  int made = 0;
  LingeringHub<Counted> hub(100);
  const auto make = [&] {
    ++made;
    return std::make_shared<Counted>(&alive);
  };
  hub.acquire(make).reset();
  EXPECT_EQ(alive, 1);
  ASSERT_TRUE(QTest::qWaitFor([&] { return alive == 0; }, 3000));
  // A later holder gets a new one.
  auto c = hub.acquire(make);
  EXPECT_EQ(made, 2);
  EXPECT_EQ(alive, 1);
}

TEST(LingeringHub, AnOldGracePeriodDoesNotReleaseANewHolder) {
  int alive = 0;
  LingeringHub<Counted> hub(100);
  const auto make = [&] { return std::make_shared<Counted>(&alive); };
  hub.acquire(make).reset();  // the grace period of the first holder starts
  auto b = hub.acquire(make);
  QTest::qWait(300);  // and ends
  EXPECT_EQ(alive, 1);
  b.reset();
  EXPECT_TRUE(QTest::qWaitFor([&] { return alive == 0; }, 3000));
}
