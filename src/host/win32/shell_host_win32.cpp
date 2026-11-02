#include <windows.h>

#include "host/qml/shell_host.h"
#include "host/win32/backdrop.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/window_focus.h"

namespace waku::host {

ShellHost::NativeHooks ShellHost::NativeHooks::win32() {
  NativeHooks hooks;
  hooks.makeAppBar = [] { return waku::host::makeWin32AppBar(); };
  hooks.applyBackdrop = [](quintptr hwnd, Backdrop::Kind backdrop) {
    switch (backdrop) {
      case Backdrop::Blur:
        waku::host::enableBlur(hwnd, true, true);
        break;
      case Backdrop::Mica:
        waku::host::enableMica(hwnd);
        break;
      case Backdrop::None:
        break;
    }
  };
  // MONITOR_DEFAULTTONEAREST: the window was only just shown, so it may not be on a monitor yet.
  hooks.monitorHandle = [](quintptr hwnd) {
    return static_cast<qlonglong>(reinterpret_cast<quintptr>(
        MonitorFromWindow(reinterpret_cast<HWND>(hwnd), MONITOR_DEFAULTTONEAREST)));
  };
  // Masks need the real cursor; without a live desktop (offscreen) they stay inert.
  if (waku::kernel::liveDesktop()) {
    hooks.cursorPosition = []() -> std::optional<QPoint> {
      POINT point;
      if (!GetCursorPos(&point)) return std::nullopt;
      return QPoint(point.x, point.y);
    };
  }
  hooks.raiseTopmost = [](quintptr hwnd) {
    SetWindowPos(reinterpret_cast<HWND>(hwnd), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  };
  hooks.setNoActivate = [](quintptr hwnd, bool noActivate) {
    HWND window = reinterpret_cast<HWND>(hwnd);
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_EXSTYLE);
    const LONG_PTR wanted = noActivate ? (style | WS_EX_NOACTIVATE) : (style & ~WS_EX_NOACTIVATE);
    if (wanted != style) SetWindowLongPtrW(window, GWL_EXSTYLE, wanted);
  };
  hooks.forceForeground = [](quintptr hwnd) { waku::kernel::forceForegroundFocus(hwnd); };
  // Fails soft: a refused call leaves the working set as it is. Not on the offscreen platform, so
  // a test run never trims the test process.
  if (waku::kernel::liveDesktop()) {
    hooks.trimWorkingSet = [] {
      SetProcessWorkingSetSizeEx(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1), 0);
    };
  }
  return hooks;
}

}  // namespace waku::host
