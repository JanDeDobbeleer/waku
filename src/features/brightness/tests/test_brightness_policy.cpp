#include <gtest/gtest.h>

#include <chrono>

#include "features/brightness/brightness_policy.h"

using namespace wabar::brightness;
using Clock = PolicyIgnoreWindow::Clock;

TEST(BrightnessPolicy, PayloadIsLittleEndianAndClamped) {
  const unsigned char fifty[] = {50, 0, 0, 0};
  EXPECT_EQ(policyBrightnessFromBytes(fifty, 4), 50);
  const unsigned char big[] = {0xE8, 0x03, 0, 0};  // 1000
  EXPECT_EQ(policyBrightnessFromBytes(big, 4), 100);
  const unsigned char shortPayload[] = {7};
  EXPECT_EQ(policyBrightnessFromBytes(shortPayload, 1), 7);
  EXPECT_EQ(policyBrightnessFromBytes(nullptr, 0), 0);
  const unsigned char longer[] = {20, 0, 0, 0, 99, 99};
  EXPECT_EQ(policyBrightnessFromBytes(longer, 6), 20);
}

TEST(BrightnessPolicy, IgnoreWindowExpiresAfterHalfASecond) {
  PolicyIgnoreWindow window;
  const auto start = Clock::now();
  EXPECT_FALSE(window.active(start));
  window.arm(start);
  EXPECT_TRUE(window.active(start + std::chrono::milliseconds(499)));
  EXPECT_FALSE(window.active(start + std::chrono::milliseconds(500)));
}

TEST(BrightnessPolicy, DefaultPollMatchesPython) { EXPECT_EQ(kDefaultDdcPollSeconds, 60); }
