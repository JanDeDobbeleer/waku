#include <windows.h>

#include "host/qml/shell_host.h"
#include "host/win32/backdrop.h"

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
  return hooks;
}

}  // namespace wabar::host
