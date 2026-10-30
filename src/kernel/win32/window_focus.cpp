#include "kernel/win32/window_focus.h"

#include <windows.h>

namespace wabar::kernel {

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

}  // namespace wabar::kernel
