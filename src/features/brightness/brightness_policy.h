#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace wabar::brightness {

// Pure helpers of the brightness service (services/brightness/service.py), kept free of Win32 so
// tests can replay them with injected time and payloads.

// Seconds between background DDC re-reads (BrightnessService.instance default); 0 disables it.
constexpr int kDefaultDdcPollSeconds = 60;
// A scheme write is followed by a power-setting notification of our own; it is ignored for this
// long (service.py `_ignore_policy_until`).
constexpr std::chrono::milliseconds kPolicyIgnoreWindow{500};

// GUID_DEVICE_POWER_POLICY_VIDEO_BRIGHTNESS payload: the first four bytes, little endian, padded
// with zeros when shorter, clamped to 0..100.
inline int policyBrightnessFromBytes(const unsigned char* data, std::size_t length) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4 && data && i < length; ++i)
    value |= static_cast<std::uint32_t>(data[i]) << (8 * i);
  return static_cast<int>(std::min<std::uint32_t>(value, 100));
}

// Thread-safe "ignore notifications until" deadline.
class PolicyIgnoreWindow {
 public:
  using Clock = std::chrono::steady_clock;

  void arm(Clock::time_point now, std::chrono::milliseconds length = kPolicyIgnoreWindow) {
    until_.store((now + length).time_since_epoch().count());
  }
  bool active(Clock::time_point now) const {
    return now.time_since_epoch().count() < until_.load();
  }

 private:
  std::atomic<Clock::rep> until_{0};
};

}  // namespace wabar::brightness
