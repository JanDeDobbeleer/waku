#pragma once

#include <optional>

#include "features/desktop_state/desktop_probe.h"

namespace wabar::desktop_state {

// Test double of DesktopProbe: public values the test edits, call counters, no Win32.
class FakeProbe final : public DesktopProbe {
 public:
  LockKeys lockKeys() override {
    ++reads;
    return keys;
  }
  KeyboardLayoutInfo keyboardLayout() override { return layout; }
  std::optional<double> idleSeconds() override { return idle; }
  void setKeepAwake(bool value) override {
    awake = value;
    ++awakeCalls;
  }

  LockKeys keys;
  KeyboardLayoutInfo layout{QStringLiteral("EN"), QStringLiteral("English (United States)"),
                            QStringLiteral("04090409")};
  std::optional<double> idle = 0.0;
  bool awake = false;
  int awakeCalls = 0;
  int reads = 0;
};

}  // namespace wabar::desktop_state
