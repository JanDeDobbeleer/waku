#include "core/bar/app_bar.h"

#include <windows.h>
#include <shellapi.h>

namespace yasb::bar {

AppBarRect computeAppBarRect(AppBarEdge edge, int height, const QRect& g, double dpr,
                             bool scaleScreenHeight) {
  const int barHeight = static_cast<int>(height * dpr);
  const int screenHeight = static_cast<int>(scaleScreenHeight ? g.height() * dpr : g.height());
  AppBarRect rc;
  rc.left = g.x();
  rc.right = g.x() + g.width();
  if (edge == AppBarEdge::Top) {
    rc.top = g.y();
    rc.bottom = g.y() + barHeight;
  } else {
    rc.top = g.y() + screenHeight - barHeight;
    rc.bottom = g.y() + screenHeight;
  }
  return rc;
}

namespace {

class Win32AppBar : public IAppBar {
 public:
  void create(const AppBarRequest& r) override {
    data_ = {};
    data_.cbSize = sizeof(data_);
    data_.uEdge = static_cast<UINT>(r.edge);
    data_.hWnd = reinterpret_cast<HWND>(r.hwnd);
    data_.uCallbackMessage = kAppBarCallbackMessage;
    active_ = true;
    SHAppBarMessage(ABM_NEW, &data_);

    LONG_PTR style = GetWindowLongPtrW(data_.hWnd, GWL_EXSTYLE) | WS_EX_NOACTIVATE;
    if (r.alwaysOnTop) style |= WS_EX_TOPMOST;
    SetWindowLongPtrW(data_.hWnd, GWL_EXSTYLE, style);

    if (r.screen) {
      const AppBarRect rc = computeAppBarRect(r.edge, r.height, r.screen->geometry(),
                                              r.screen->devicePixelRatio(), r.scaleScreenHeight);
      data_.rc = {rc.left, rc.top, rc.right, rc.bottom};
    }
    if (r.reserveSpace) SHAppBarMessage(ABM_SETPOS, &data_);
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

}  // namespace yasb::bar
