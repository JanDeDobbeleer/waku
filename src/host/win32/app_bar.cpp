#include "host/app_bar.h"

#include <windows.h>
#include <shellapi.h>

#include <cmath>

namespace wabar::host {

namespace {

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
  void create(const AppBarRequest& r) override {
    HWND hwnd = reinterpret_cast<HWND>(r.hwnd);
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (r.noActivate) style |= WS_EX_NOACTIVATE;
    if (r.alwaysOnTop) style |= WS_EX_TOPMOST;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style);

    // A window that does not reserve space is never registered, so a run without
    // --register-app-bar leaves the shell's app bar list alone.
    if (!r.reserveSpace || !r.screen || r.thickness <= 0) return;

    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.uEdge = static_cast<UINT>(r.edge);
    data_.hWnd = hwnd;
    data_.uCallbackMessage = kAppBarCallbackMessage;
    active_ = SHAppBarMessage(ABM_NEW, &data_) != 0;
    if (!active_) return;

    Win32AppBarSystem system(hwnd);
    const double dpr = r.screen->devicePixelRatio();
    const AppBarGrant grant =
        negotiateAppBarRect(system, r.edge, static_cast<int>(std::lround(r.thickness * dpr)),
                            physicalScreenRect(r.screen->geometry(), dpr));
    // Another app bar or the taskbar may hold the edge: move the window to where it was granted.
    if (grant.inset > 0) {
      RECT window{};
      if (GetWindowRect(hwnd, &window)) {
        const QPoint shift = appBarWindowShift(r.edge, grant.inset);
        SetWindowPos(hwnd, nullptr, window.left + shift.x(), window.top + shift.y(), 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
    }
  }

  void remove() override {
    if (!active_) return;
    SHAppBarMessage(ABM_REMOVE, &data_);
    active_ = false;
  }

 private:
  APPBARDATA data_{};
  bool active_ = false;
};

}  // namespace

std::unique_ptr<IAppBar> makeWin32AppBar() { return std::make_unique<Win32AppBar>(); }

}  // namespace wabar::host
