#include <windows.h>

#include "host/qml/shell_host.h"
#include "host/win32/backdrop.h"
#include "kernel/win32/window_focus.h"

namespace wabar::host {

ShellHost::NativeHooks ShellHost::NativeHooks::win32() {
  NativeHooks hooks;
  hooks.makeAppBar = [] { return wabar::host::makeWin32AppBar(); };
  hooks.applyBackdrop = [](quintptr hwnd, Backdrop::Kind backdrop) {
    switch (backdrop) {
      case Backdrop::Blur:
        wabar::host::enableBlur(hwnd, true, true);
        break;
      case Backdrop::Mica:
        wabar::host::enableMica(hwnd);
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
  hooks.forceForeground = [](quintptr hwnd) { wabar::kernel::forceForegroundFocus(hwnd); };
  return hooks;
}

}  // namespace wabar::host
