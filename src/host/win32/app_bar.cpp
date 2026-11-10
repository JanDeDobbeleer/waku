#include "host/app_bar.h"

#include <windows.h>
#include <shellapi.h>

#include <cmath>
#include <cwchar>
#include <vector>

namespace waku::host {

namespace {

// The tray window host (features/tray) is a Shell_TrayWnd window of this process. While the Windows
// taskbar is hidden it is topmost, so FindWindow, and with it SHAppBarMessage, finds it before
// Explorer's. SHAppBarMessage then puts the request in shared memory that only the process of the
// window it found can open, and the host passes the request on to Explorer, which cannot open it:
// the app bar is not registered and the work area is not reduced. While one of our own app bar
// calls runs, the host window drops out of the topmost band so Explorer's window is found; it goes
// back on top at once. Documented calls only; nothing happens when there is no such window.
class TrayWindowYield {
 public:
  TrayWindowYield() {
    EnumWindows(&TrayWindowYield::collect, reinterpret_cast<LPARAM>(this));
    for (HWND window : windows_) {
      SetWindowPos(window, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
  ~TrayWindowYield() {
    for (HWND window : windows_) {
      SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
  }
  TrayWindowYield(const TrayWindowYield&) = delete;
  TrayWindowYield& operator=(const TrayWindowYield&) = delete;

 private:
  static BOOL CALLBACK collect(HWND window, LPARAM param) {
    wchar_t className[32] = {};
    if (!GetClassNameW(window, className, 32) || wcscmp(className, L"Shell_TrayWnd") != 0) {
      return TRUE;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return TRUE;
    if (!(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)) return TRUE;
    reinterpret_cast<TrayWindowYield*>(param)->windows_.push_back(window);
    return TRUE;
  }

  std::vector<HWND> windows_;
};

// ABM_QUERYPOS and ABM_SETPOS on one registered window; rects are physical pixels.
class Win32AppBarSystem final : public IAppBarSystem {
 public:
  explicit Win32AppBarSystem(HWND hwnd) {
    data_.cbSize = sizeof(data_);
    data_.hWnd = hwnd;
  }

  AppBarRect queryPos(AppBarEdge edge, const AppBarRect& proposed) override {
    return send(ABM_QUERYPOS, edge, proposed);
  }
  AppBarRect setPos(AppBarEdge edge, const AppBarRect& proposed) override {
    return send(ABM_SETPOS, edge, proposed);
  }

 private:
  AppBarRect send(DWORD message, AppBarEdge edge, const AppBarRect& rect) {
    data_.uEdge = static_cast<UINT>(edge);
    data_.rc = {rect.left, rect.top, rect.right, rect.bottom};
    SHAppBarMessage(message, &data_);
    return {data_.rc.left, data_.rc.top, data_.rc.right, data_.rc.bottom};
  }

  APPBARDATA data_{};
};

class Win32AppBar : public IAppBar {
 public:
  int create(const AppBarRequest& r) override {
    TrayWindowYield yield;
    HWND hwnd = reinterpret_cast<HWND>(r.hwnd);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (r.noActivate) style |= WS_EX_NOACTIVATE;
    if (r.alwaysOnTop) style |= WS_EX_TOPMOST;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);

    // A window that does not reserve space is never registered, so a run without
    // --no-app-bar leaves the shell's app bar list alone.
    if (!r.reserveSpace || !r.screen || r.thickness <= 0) return 0;

    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.uEdge = static_cast<UINT>(r.edge);
    data_.hWnd = hwnd;
    data_.uCallbackMessage = kAppBarCallbackMessage;
    active_ = SHAppBarMessage(ABM_NEW, &data_) != 0;
    if (!active_) return 0;
    return negotiate(r);
  }

  int reposition(const AppBarRequest& r) override {
    if (!active_ || !r.reserveSpace || !r.screen || r.thickness <= 0) return create(r);
    TrayWindowYield yield;
    data_.uEdge = static_cast<UINT>(r.edge);
    return negotiate(r);
  }

  void remove() override {
    if (!active_) return;
    TrayWindowYield yield;
    SHAppBarMessage(ABM_REMOVE, &data_);
    active_ = false;
  }

 private:
  // ABM_QUERYPOS then ABM_SETPOS. Another app bar or the taskbar may hold the edge; the inset is
  // how far the bar was pushed in, and the host moves the window by it.
  int negotiate(const AppBarRequest& r) {
    Win32AppBarSystem system(data_.hWnd);
    const double dpr = r.screen->devicePixelRatio();
    return negotiateAppBarRect(system, r.edge, static_cast<int>(std::lround(r.thickness * dpr)),
                               physicalScreenRect(r.screen->geometry(), dpr))
        .inset;
  }

  APPBARDATA data_{};
  bool active_ = false;
};

}  // namespace

std::unique_ptr<IAppBar> makeWin32AppBar() { return std::make_unique<Win32AppBar>(); }

}  // namespace waku::host
