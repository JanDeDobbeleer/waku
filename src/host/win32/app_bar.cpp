#include "host/app_bar.h"

#include <windows.h>
#include <shellapi.h>

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QDebug>
#include <QObject>
#include <QTimer>
#include <cmath>
#include <exception>

#include "host/win32/tray_window_yield.h"

namespace waku::host {

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

class Win32AppBar : public QObject, public IAppBar, public QAbstractNativeEventFilter {
 public:
  Win32AppBar() {
    positionTimer_.setSingleShot(true);
    connect(&positionTimer_, &QTimer::timeout, this, [this] {
      try {
        if (active_ && positionChanged) positionChanged();
      } catch (const std::exception& e) {
        qWarning() << "app bar: renegotiation failed:" << e.what();
      } catch (...) {
        qWarning() << "app bar: renegotiation failed";
      }
    });
    QCoreApplication::instance()->installNativeEventFilter(this);
  }

  ~Win32AppBar() override { QCoreApplication::instance()->removeNativeEventFilter(this); }

  bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
    const auto* event = static_cast<MSG*>(message);
    if (active_ && event->hwnd == data_.hWnd && event->message == kAppBarCallbackMessage &&
        event->wParam == ABN_POSCHANGED) {
      // Explorer can notify synchronously from SETPOS. Defer until negotiation has unwound.
      if (!positionTimer_.isActive()) positionTimer_.start(0);
    }
    return false;
  }

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
    positionTimer_.stop();
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
  QTimer positionTimer_;
};

}  // namespace

std::unique_ptr<IAppBar> makeWin32AppBar() { return std::make_unique<Win32AppBar>(); }

}  // namespace waku::host
