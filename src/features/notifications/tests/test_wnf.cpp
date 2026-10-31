#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include "features/notifications/win32/wnf_win.h"

using namespace wabar::notifications;

namespace {

// A fake ntdll: records the callback and context so the test can call back as WNF would.
struct FakeWnf {
  static inline WnfSubscription::Callback callback = nullptr;
  static inline void* context = nullptr;
  static inline long status = 0;
  static inline int unsubscribes = 0;

  static long __stdcall subscribe(void** subscription, std::uint64_t, unsigned long,
                                  WnfSubscription::Callback cb, void* ctx, void*, unsigned long,
                                  unsigned long) {
    callback = cb;
    context = ctx;
    *subscription = &callback;
    return status;
  }
  static long __stdcall unsubscribe(void*) {
    ++unsubscribes;
    return 0;
  }
  static WnfSubscription::Api api() {
    callback = nullptr;
    context = nullptr;
    status = 0;
    unsubscribes = 0;
    return {&FakeWnf::subscribe, &FakeWnf::unsubscribe};
  }
  static void fire() { callback(0, 0, nullptr, context, nullptr, 0); }
};

}  // namespace

TEST(NotificationsWnf, RunsTheHandlerWhileSubscribed) {
  WnfSubscription wnf(FakeWnf::api());
  int calls = 0;
  ASSERT_EQ(wnf.subscribe(1, [&](const void*, unsigned long) { ++calls; }), 0);
  FakeWnf::fire();
  EXPECT_EQ(calls, 1);
  wnf.unsubscribe();
  EXPECT_EQ(FakeWnf::unsubscribes, 1);
  // A callback WNF delivers late finds nothing to run.
  FakeWnf::fire();
  EXPECT_EQ(calls, 1);
}

TEST(NotificationsWnf, ALateCallbackAfterDestructionIsANoOp) {
  int calls = 0;
  {
    WnfSubscription wnf(FakeWnf::api());
    ASSERT_EQ(wnf.subscribe(1, [&](const void*, unsigned long) { ++calls; }), 0);
  }
  FakeWnf::fire();
  EXPECT_EQ(calls, 0);
}

TEST(NotificationsWnf, AFailedSubscribeLeavesNothingRegistered) {
  WnfSubscription wnf(FakeWnf::api());
  FakeWnf::status = 0x123;
  int calls = 0;
  EXPECT_EQ(wnf.subscribe(1, [&](const void*, unsigned long) { ++calls; }), 0x123);
  EXPECT_FALSE(wnf.active());
  EXPECT_EQ(FakeWnf::unsubscribes, 0);
  FakeWnf::fire();
  EXPECT_EQ(calls, 0);
}

TEST(NotificationsWnf, MissingApiIsUnavailable) {
  WnfSubscription wnf(WnfSubscription::Api{});
  EXPECT_FALSE(wnf.available());
  EXPECT_EQ(wnf.subscribe(1, [](const void*, unsigned long) {}), -1);
}

TEST(NotificationsWnf, UnsubscribeWaitsForARunningHandler) {
  WnfSubscription wnf(FakeWnf::api());
  std::promise<void> entered;
  std::promise<void> release;
  std::shared_future<void> released = release.get_future().share();
  std::atomic<bool> finished = false;
  ASSERT_EQ(wnf.subscribe(1,
                          [&](const void*, unsigned long) {
                            entered.set_value();
                            released.wait();
                            finished = true;
                          }),
            0);
  std::thread system([] { FakeWnf::fire(); });
  entered.get_future().wait();

  auto stopped = std::async(std::launch::async, [&] { wnf.unsubscribe(); });
  EXPECT_EQ(stopped.wait_for(std::chrono::milliseconds(100)), std::future_status::timeout);
  release.set_value();
  stopped.wait();
  EXPECT_TRUE(finished);
  system.join();
}
