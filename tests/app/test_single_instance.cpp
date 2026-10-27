#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

#include "app/single_instance.h"

TEST(SingleInstance, RejectsSecondInstanceAndAllowsRestartWaitAfterRelease) {
  const std::wstring name = L"yasb_native_test_" + std::to_wstring(GetCurrentProcessId());
  auto first = std::make_unique<yasb::app::SingleInstance>(name);
  ASSERT_TRUE(first->acquire(false));

  yasb::app::SingleInstance second(name);
  EXPECT_FALSE(second.acquire(false));
  EXPECT_NE(second.error().find("already running"), std::string::npos);

  std::atomic<bool> acquired = false;
  std::thread waiter([&] {
    yasb::app::SingleInstance replacement(name);
    acquired.store(replacement.acquire(true, 3000));
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  first.reset();
  waiter.join();
  EXPECT_TRUE(acquired.load());
}
