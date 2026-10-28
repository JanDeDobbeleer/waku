#include "platform/window_focus.h"

#include <windows.h>

namespace yasb::platform {

void forceForegroundFocus(std::uintptr_t handle) {
  HWND hwnd = reinterpret_cast<HWND>(handle);
  if (!hwnd) return;
  HWND foreground = GetForegroundWindow();
  const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
  const DWORD ownThread = GetCurrentThreadId();
  if (foregroundThread && ownThread && foregroundThread != ownThread) {
    AttachThreadInput(ownThread, foregroundThread, TRUE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    AttachThreadInput(ownThread, foregroundThread, FALSE);
  } else {
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
  }
}

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

}  // namespace yasb::platform
