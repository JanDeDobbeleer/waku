#include <gtest/gtest.h>

#include <QTest>
#include <memory>
#include <utility>

#include "kernel/qt/linger.h"

using waku::kernel::linger;
using waku::kernel::LingeringHub;

namespace {

// The counter is shared with the object: a lingering object can outlive the test that made it and
// is destroyed by a later test's event loop, so it must not point into the finished test's stack.
struct Counted {
  explicit Counted(std::shared_ptr<int> alive) : alive_(std::move(alive)) { ++*alive_; }
  ~Counted() { --*alive_; }
  std::shared_ptr<int> alive_;
};

}  // namespace

TEST(Linger, KeepsTheObjectUntilTheTimeIsUp) {
  auto alive = std::make_shared<int>(0);
  linger(std::make_shared<Counted>(alive), 100);
  EXPECT_EQ(*alive, 1);
  EXPECT_TRUE(QTest::qWaitFor([&] { return *alive == 0; }, 3000));
}

TEST(Linger, NoTimeReleasesAtOnce) {
  auto alive = std::make_shared<int>(0);
  linger(std::make_shared<Counted>(alive), 0);
  EXPECT_EQ(*alive, 0);
}

TEST(LingeringHub, HoldersShareOneObject) {
  auto alive = std::make_shared<int>(0);
  int made = 0;
  LingeringHub<Counted> hub(100);
  const auto make = [&] {
    ++made;
    return std::make_shared<Counted>(alive);
  };
  auto a = hub.acquire(make);
  auto b = hub.acquire(make);
  EXPECT_EQ(a.get(), b.get());
  EXPECT_EQ(made, 1);
  EXPECT_EQ(*alive, 1);
}

TEST(LingeringHub, AHolderWithinTheGracePeriodGetsTheSameObject) {
  auto alive = std::make_shared<int>(0);
  int made = 0;
  LingeringHub<Counted> hub(2000);
  const auto make = [&] {
    ++made;
    return std::make_shared<Counted>(alive);
  };
  Counted* first = nullptr;
  {
    auto a = hub.acquire(make);
    first = a.get();
  }
  // The last holder is gone (as during a reload), the object is not.
  EXPECT_EQ(*alive, 1);
  auto b = hub.acquire(make);
  EXPECT_EQ(b.get(), first);
  EXPECT_EQ(made, 1);
}

TEST(LingeringHub, TheObjectGoesWhenNobodyComesBack) {
  auto alive = std::make_shared<int>(0);
  int made = 0;
  LingeringHub<Counted> hub(100);
  const auto make = [&] {
    ++made;
    return std::make_shared<Counted>(alive);
  };
  hub.acquire(make).reset();
  EXPECT_EQ(*alive, 1);
  ASSERT_TRUE(QTest::qWaitFor([&] { return *alive == 0; }, 3000));
  // A later holder gets a new one.
  auto c = hub.acquire(make);
  EXPECT_EQ(made, 2);
  EXPECT_EQ(*alive, 1);
}

TEST(LingeringHub, AnOldGracePeriodDoesNotReleaseANewHolder) {
  auto alive = std::make_shared<int>(0);
  LingeringHub<Counted> hub(100);
  std::weak_ptr<Counted> object;
  const auto make = [&] {
    auto made = std::make_shared<Counted>(alive);
    object = made;
    return made;
  };
  hub.acquire(make).reset();  // the grace period of the first holder starts
  auto b = hub.acquire(make);
  ASSERT_EQ(object.use_count(), 2);  // the lingering reference and the one behind `b`
  // The grace period ends when linger() drops its reference.
  ASSERT_TRUE(QTest::qWaitFor([&] { return object.use_count() == 1; }, 3000));
  EXPECT_EQ(*alive, 1);
  b.reset();
  EXPECT_TRUE(QTest::qWaitFor([&] { return *alive == 0; }, 3000));
}
