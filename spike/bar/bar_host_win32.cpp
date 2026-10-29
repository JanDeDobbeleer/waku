#include <windows.h>

#include "core/widgets/backdrop.h"
#include "spike/bar/bar_host.h"

namespace wabar {

BarHost::NativeHooks BarHost::NativeHooks::win32() {
  NativeHooks hooks;
  hooks.makeAppBar = [] { return yasb::bar::makeWin32AppBar(); };
  hooks.applyBackdrop = [](quintptr hwnd, Bar::Backdrop backdrop) {
    switch (backdrop) {
      case Bar::Blur:
        yasb::widgets::enableBlur(hwnd, true, true);
        break;
      case Bar::Mica:
        yasb::widgets::enableMica(hwnd);
        break;
      case Bar::None:
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

}  // namespace wabar
