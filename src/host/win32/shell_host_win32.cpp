#include <windows.h>
#include <dwmapi.h>

#include <cwchar>

#include "host/panel_geometry.h"
#include "host/qml/shell_host.h"
#include "host/win32/backdrop.h"
#include "kernel/qt/live_desktop.h"
#include "kernel/win32/window_focus.h"

namespace waku::host {

namespace {

struct FullscreenSearch {
  RECT monitor;
  RECT panel;
  bool found = false;
};

BOOL CALLBACK findFullscreenWindow(HWND window, LPARAM parameter) {
  auto& search = *reinterpret_cast<FullscreenSearch*>(parameter);
  if (!IsWindowVisible(window) || IsIconic(window)) return TRUE;
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == GetCurrentProcessId()) return TRUE;
  if (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
  DWORD cloaked = 0;
  if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) &&
      cloaked) {
    return TRUE;
  }
  wchar_t name[64] = {};
  GetClassNameW(window, name, 64);
  if (wcscmp(name, L"Progman") == 0 || wcscmp(name, L"WorkerW") == 0 ||
      wcscmp(name, L"Shell_TrayWnd") == 0 || wcscmp(name, L"Shell_SecondaryTrayWnd") == 0) {
    return TRUE;
  }
  RECT rect{};
  RECT intersection{};
  if (!GetWindowRect(window, &rect) || !IntersectRect(&intersection, &rect, &search.panel)) {
    return TRUE;
  }
  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  const QRect bounds(rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
  const QRect monitor(search.monitor.left, search.monitor.top,
                      search.monitor.right - search.monitor.left,
                      search.monitor.bottom - search.monitor.top);
  search.found = coversScreenForFullscreen(bounds, monitor, IsZoomed(window) != FALSE,
                                           (style & WS_CAPTION) == WS_CAPTION);
  return FALSE;
}

bool hasFullscreenWindow(HWND panel) {
  const HMONITOR monitor = MonitorFromWindow(panel, MONITOR_DEFAULTTONULL);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  FullscreenSearch search{};
  if (!monitor || !GetMonitorInfoW(monitor, &info) || !GetWindowRect(panel, &search.panel)) {
    return false;
  }
  search.monitor = info.rcMonitor;
  EnumWindows(findFullscreenWindow, reinterpret_cast<LPARAM>(&search));
  return search.found;
}

}  // namespace

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
    hooks.fullscreenOnMonitor = [](quintptr hwnd) {
      return hasFullscreenWindow(reinterpret_cast<HWND>(hwnd));
    };
    hooks.lowerToBottom = [](quintptr hwnd) {
      SetWindowPos(reinterpret_cast<HWND>(hwnd), HWND_BOTTOM, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
