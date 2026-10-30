#include <windows.h>

#include <memory>

#include "features/taskbar/window_control_backend.h"
#include "kernel/win32/window_focus.h"

namespace wabar::taskbar {
namespace {

HWND toHwnd(std::uintptr_t hwnd) { return reinterpret_cast<HWND>(hwnd); }

// Documented Win32 only: IsWindow, IsIconic, GetForegroundWindow, ShowWindowAsync, FlashWindowEx.
class Win32WindowControlBackend final : public WindowControlBackend {
 public:
  bool isWindow(std::uintptr_t hwnd) const override { return hwnd && IsWindow(toHwnd(hwnd)); }

  bool isIconic(std::uintptr_t hwnd) const override { return hwnd && IsIconic(toHwnd(hwnd)); }

  std::uintptr_t foreground() const override {
    return reinterpret_cast<std::uintptr_t>(GetForegroundWindow());
  }

  // Asynchronous, so a window that does not pump messages cannot block the GUI thread. Works on
  // elevated windows, where a posted message would be dropped (UIPI).
  void restore(std::uintptr_t hwnd) override { ShowWindowAsync(toHwnd(hwnd), SW_RESTORE); }

  bool bringToFront(std::uintptr_t hwnd) override {
    kernel::forceForegroundFocus(hwnd);
    if (foreground() == hwnd) return true;
    FLASHWINFO info{};
    info.cbSize = sizeof(info);
    info.hwnd = toHwnd(hwnd);
    info.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
    FlashWindowEx(&info);
    return false;
  }
};

}  // namespace

std::shared_ptr<WindowControlBackend> makeWin32WindowControlBackend() {
  return std::make_shared<Win32WindowControlBackend>();
}

}  // namespace wabar::taskbar
