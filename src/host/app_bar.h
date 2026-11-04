#pragma once

#include <QRect>
#include <QScreen>
#include <cstdint>
#include <memory>
#include <vector>

namespace waku::host {

// The values are the ABE_* constants of SHAppBarMessage.
enum class AppBarEdge { Left = 0, Top = 1, Right = 2, Bottom = 3 };

constexpr unsigned kAppBarCallbackMessage = 0x0400 + 100;  // WM_USER + 100

// True for Left and Right: the bar's thickness is its width.
constexpr bool isVerticalEdge(AppBarEdge edge) {
  return edge == AppBarEdge::Left || edge == AppBarEdge::Right;
}

struct AppBarRequest {
  std::uintptr_t hwnd = 0;
  AppBarEdge edge = AppBarEdge::Top;
  // The pixels to reserve across the bar: a top or bottom bar's height, a side bar's width, plus
  // the margin on that edge. Logical pixels.
  int thickness = 0;
  QScreen* screen = nullptr;
  // The screen's device pixel ratio when the request was made. Part of the request so that a scale
  // change alone (same screen, same logical thickness) re-negotiates the width in physical pixels.
  double dpr = 1.0;
  bool reserveSpace = false;
  bool alwaysOnTop = false;
  // WS_EX_NOACTIVATE: a click on the bar does not take the focus (keyboardFocus None).
  bool noActivate = true;
  bool operator==(const AppBarRequest&) const = default;
};

// A RECT in physical pixels (what SHAppBarMessage reads and writes).
struct AppBarRect {
  int left = 0, top = 0, right = 0, bottom = 0;
  bool operator==(const AppBarRect&) const = default;
  int width() const { return right - left; }
  int height() const { return bottom - top; }
};

// The physical-pixel rectangle of a screen. Qt reports `QScreen::geometry()` with the origin in
// native (physical) desktop coordinates and the size in device-independent pixels, so the origin
// is kept and the size is multiplied by `dpr`.
AppBarRect physicalScreenRect(const QRect& geometry, double dpr);

// `screen` with the thickness side moved to `thicknessPx` along `edge`: the rect an app bar on
// that edge asks for. Top and Left keep the screen's top or left; Bottom and Right keep the
// bottom or right. The other axis spans the whole screen.
AppBarRect computeAppBarRect(AppBarEdge edge, int thicknessPx, const AppBarRect& screen);
// The same from a QScreen's geometry and device pixel ratio; `thickness` in logical pixels.
AppBarRect computeAppBarRect(AppBarEdge edge, int thickness, const QRect& screenGeometry,
                             double dpr);

// The two calls SHAppBarMessage makes to place an app bar (ABM_QUERYPOS, ABM_SETPOS). Each takes
// the rect asked for and returns the rect the system adjusted it to (another app bar, the Windows
// taskbar). A seam so the negotiation is tested without a desktop.
class IAppBarSystem {
 public:
  virtual ~IAppBarSystem() = default;
  virtual AppBarRect queryPos(AppBarEdge edge, const AppBarRect& proposed) = 0;
  virtual AppBarRect setPos(AppBarEdge edge, const AppBarRect& proposed) = 0;
};

struct AppBarGrant {
  AppBarRect requested;  // the rect asked for first
  AppBarRect granted;    // the rect ABM_SETPOS left
  // How far the system pushed the bar inward from the screen edge, physical pixels, >= 0.
  int inset = 0;
};

// ABM_QUERYPOS first, as the API describes, then ABM_SETPOS with the thickness side re-applied to
// the adjusted rect; the result is what SETPOS read back. Pure apart from `system`.
AppBarGrant negotiateAppBarRect(IAppBarSystem& system, AppBarEdge edge, int thicknessPx,
                                const AppBarRect& screen);

// The offset to add to a window's physical position so it sits inside the granted rect: `inset`
// pixels away from `edge` (down for Top, up for Bottom, right for Left, left for Right).
QPoint appBarWindowShift(AppBarEdge edge, int inset);

// Seam so tests and --config-home runs never touch the live desktop's app-bar list.
//
// `create` and `reposition` return how far the system pushed the bar inward from the screen edge,
// in physical pixels (0 when nothing is reserved or the edge was free). They do not move the
// window: the host stores the inset on the window, which applies it with every geometry pass.
class IAppBar {
 public:
  virtual ~IAppBar() = default;
  // Sets the window styles and, when the request reserves space, registers the bar and asks for
  // its rect (ABM_NEW, ABM_QUERYPOS, ABM_SETPOS).
  virtual int create(const AppBarRequest& request) = 0;
  // Asks again for the rect after the thickness, the screen or the margins changed. Without a
  // registered bar it does what `create` does.
  virtual int reposition(const AppBarRequest& request) = 0;
  virtual void remove() = 0;
};

// Records requests; registers nothing. `inset` is what every call grants.
class FakeAppBar : public IAppBar {
 public:
  int create(const AppBarRequest& request) override {
    requests.push_back(request);
    return inset;
  }
  int reposition(const AppBarRequest& request) override {
    repositions.push_back(request);
    return inset;
  }
  void remove() override { ++removeCount; }
  std::vector<AppBarRequest> requests;
  std::vector<AppBarRequest> repositions;
  int removeCount = 0;
  int inset = 0;
};

// Talks to SHAppBarMessage and sets WS_EX_NOACTIVATE / WS_EX_TOPMOST. Live desktop only. It
// registers the window as an app bar (ABM_NEW) only when the request reserves space.
std::unique_ptr<IAppBar> makeWin32AppBar();

}  // namespace waku::host
