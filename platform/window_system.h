#pragma once

#include <QByteArray>
#include <QRect>
#include <QString>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace yasb::platform {

// An HWND as an integer. 0 means "no window".
using WindowHandle = std::uintptr_t;

// Port of ApplicationWindow.as_dict(): what the taskbar knows about a top-level window.
struct WindowInfo {
  WindowHandle hwnd = 0;
  QString title;
  QString className;
  bool isActive = false;
  bool isFlashing = false;
  bool isCloaked = false;
  // 0 when MonitorFromWindow found no monitor (transient).
  WindowHandle monitor = 0;
  // nullopt when the process could not be opened (access denied or gone).
  std::optional<QString> processName;
  std::uint32_t pid = 0;
  QString processPath;
  bool canMinimize = false;

  bool operator==(const WindowInfo&) const = default;
};

// 32-bit BGRA, straight (not premultiplied) alpha, top-down. QtGui is off limits here, so the
// widget turns this into a QImage (Format_ARGB32).
struct IconBitmap {
  int width = 0;
  int height = 0;
  QByteArray bgra;
  // False when the window did not answer WM_GETICON in time; the caller asks again later.
  bool complete = true;
  bool isNull() const { return width <= 0 || height <= 0 || bgra.isEmpty(); }
};

// Every OS fact and action the taskbar needs. The Win32 implementation is stateless and safe to
// call from any thread; tests substitute a fake so nothing real is ever activated or closed.
class WindowSystem {
 public:
  virtual ~WindowSystem() = default;

  // --- state
  virtual bool isWindow(WindowHandle hwnd) const = 0;
  virtual bool isIconic(WindowHandle hwnd) const = 0;
  virtual WindowHandle foregroundWindow() const = 0;
  // Every top-level window, in z order (EnumWindows).
  virtual std::vector<WindowHandle> enumerateWindows() const = 0;
  // A fresh read of every field. isActive and isFlashing are always false (the tracker owns them).
  virtual WindowInfo query(WindowHandle hwnd) const = 0;
  // ApplicationWindow.is_taskbar_window(): top level, titled, not immersive shell, passes the
  // taskbar policy and the built-in ignore lists. Uses info.title/className as given.
  virtual bool isTaskbarWindow(const WindowInfo& info) const = 0;
  virtual bool canMinimize(WindowHandle hwnd) const = 0;
  virtual WindowHandle monitorOfWindow(WindowHandle hwnd) const = 0;
  // Screen rectangle of the window; a null rect when it cannot be read.
  virtual QRect windowRect(WindowHandle hwnd) const = 0;
  // (root owner, last active visible popup of it); both equal hwnd when nothing resolves.
  virtual std::pair<WindowHandle, WindowHandle> resolveBaseAndFocus(WindowHandle hwnd) const = 0;
  virtual bool isOwnerRootActive(WindowHandle base) const = 0;
  // PinManager.get_app_unique_id: "aumid:..", "explorer:..", "path:..[|args]", nullopt when the
  // app cannot be identified.
  virtual std::optional<QString> appUniqueId(const WindowInfo& info) const = 0;
  // The window's icon at `physicalSize` pixels (blocking; callers run it on a worker thread).
  virtual IconBitmap windowIcon(WindowHandle hwnd, int physicalSize) const = 0;

  // --- actions
  virtual void restoreWindow(WindowHandle hwnd) = 0;
  virtual void minimizeWindow(WindowHandle hwnd) = 0;
  virtual void showWindow(WindowHandle hwnd) = 0;
  virtual void setForeground(WindowHandle hwnd) = 0;
  // Graceful close (SC_CLOSE) or, with `force`, EndTask / TerminateProcess ("End task").
  virtual void closeApplication(WindowHandle hwnd, bool force) = 0;
};

std::shared_ptr<WindowSystem> makeWin32WindowSystem();

}  // namespace yasb::platform
