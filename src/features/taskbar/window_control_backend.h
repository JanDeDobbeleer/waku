#pragma once

#include <cstdint>
#include <memory>

namespace wabar::taskbar {

// What `WindowControl` needs from the OS, for a window known only by its HWND. The Win32
// implementation is stateless; tests substitute a fake so no real window is ever touched.
class WindowControlBackend {
 public:
  virtual ~WindowControlBackend() = default;

  virtual bool isWindow(std::uintptr_t hwnd) const = 0;
  virtual bool isIconic(std::uintptr_t hwnd) const = 0;
  // 0 when there is no foreground window.
  virtual std::uintptr_t foreground() const = 0;
  // Asks the window to leave the minimised state.
  virtual void restore(std::uintptr_t hwnd) = 0;
  // Brings the window to the front and gives it focus. True when it is the foreground window
  // afterwards. When Windows' foreground lock refuses, the taskbar button of the window flashes
  // instead and the result is false.
  virtual bool bringToFront(std::uintptr_t hwnd) = 0;
};

std::shared_ptr<WindowControlBackend> makeWin32WindowControlBackend();

}  // namespace wabar::taskbar
