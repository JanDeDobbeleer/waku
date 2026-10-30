#include "features/wallpapers/win32/window_focus.h"

#include <windows.h>

namespace wabar::wallpapers {

std::optional<QRect> foregroundMonitorRect() {
  HWND foreground = GetForegroundWindow();
  if (!foreground) return std::nullopt;
  HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL);
  if (!monitor) return std::nullopt;
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) return std::nullopt;
  const RECT& r = info.rcMonitor;
  return QRect(r.left, r.top, r.right - r.left, r.bottom - r.top);
}

}  // namespace wabar::wallpapers
