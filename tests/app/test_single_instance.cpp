#include <gtest/gtest.h>

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

  const auto started = std::chrono::steady_clock::now();
  EXPECT_FALSE(second.acquire(true, 50));
  EXPECT_NE(second.error().find("Timeout"), std::string::npos)
      << "restart wait must report a bounded timeout while the old instance owns the mutex";
  EXPECT_LT(std::chrono::steady_clock::now() - started, std::chrono::seconds(3))
      << "restart wait exceeded its test deadline";

  first.reset();
  yasb::app::SingleInstance replacement(name);
  EXPECT_TRUE(replacement.acquire(true, 3000)) << replacement.error();
}
