#pragma once

#include <windows.h>

#include <cwchar>
#include <vector>

namespace waku::host {

// SHAppBarMessage must reach Explorer directly: forwarding through our tray window breaks its
// shared-memory exchange. A new tray window can win the lookup before it becomes topmost.
// Tests use a private window class, never the real tray.
class TrayWindowYield {
 public:
  explicit TrayWindowYield(const wchar_t* windowClass = L"Shell_TrayWnd")
      : windowClass_(windowClass) {
    EnumWindows(&TrayWindowYield::collect, reinterpret_cast<LPARAM>(this));
    for (const auto& window : windows_) {
      SetWindowPos(window.hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
  ~TrayWindowYield() {
    for (const auto& window : windows_) {
      SetWindowPos(window.hwnd, window.topmost ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
  TrayWindowYield(const TrayWindowYield&) = delete;
  TrayWindowYield& operator=(const TrayWindowYield&) = delete;

 private:
  struct YieldedWindow {
    HWND hwnd;
    bool topmost;
  };

  static BOOL CALLBACK collect(HWND window, LPARAM param) {
    auto* self = reinterpret_cast<TrayWindowYield*>(param);
    wchar_t className[32] = {};
    if (!GetClassNameW(window, className, 32) || wcscmp(className, self->windowClass_) != 0) {
      return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    const bool topmost = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    self->windows_.push_back({window, topmost});
    return TRUE;
  }

  const wchar_t* windowClass_;
  std::vector<YieldedWindow> windows_;
};

}  // namespace waku::host
