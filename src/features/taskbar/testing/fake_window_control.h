#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>
#include <set>

#include "features/taskbar/window_control_backend.h"

namespace wabar::taskbar {

// A desktop of a few windows. `log` records the calls that change something, in order.
class FakeWindowControl final : public WindowControlBackend {
 public:
  std::set<std::uintptr_t> windows;
  std::set<std::uintptr_t> minimized;
  std::uintptr_t foregroundWindow = 0;
  // When true, bringToFront models the foreground lock refusing.
  bool refuseForeground = false;
  QStringList log;

  bool isWindow(std::uintptr_t hwnd) const override { return windows.count(hwnd) != 0; }
  bool isIconic(std::uintptr_t hwnd) const override { return minimized.count(hwnd) != 0; }
  std::uintptr_t foreground() const override { return foregroundWindow; }

  void restore(std::uintptr_t hwnd) override {
    log << QStringLiteral("restore %1").arg(hwnd);
    minimized.erase(hwnd);
  }

  bool bringToFront(std::uintptr_t hwnd) override {
    log << QStringLiteral("front %1").arg(hwnd);
    if (refuseForeground) return false;
    foregroundWindow = hwnd;
    return true;
  }
};

}  // namespace wabar::taskbar
