#pragma once

#include <QRect>
#include <QScreen>
#include <cstdint>
#include <memory>
#include <vector>

namespace wabar::host {

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
  bool reserveSpace = false;
  bool alwaysOnTop = false;
  // WS_EX_NOACTIVATE: a click on the bar does not take the focus (keyboardFocus None).
  bool noActivate = true;
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
class IAppBar {
 public:
  virtual ~IAppBar() = default;
  virtual void create(const AppBarRequest& request) = 0;
  virtual void remove() = 0;
};

// Records requests; registers nothing.
class FakeAppBar : public IAppBar {
 public:
  void create(const AppBarRequest& request) override { requests.push_back(request); }
  void remove() override { ++removeCount; }
  std::vector<AppBarRequest> requests;
  int removeCount = 0;
};

// Talks to SHAppBarMessage and sets WS_EX_NOACTIVATE / WS_EX_TOPMOST. Live desktop only. It
// registers the window as an app bar (ABM_NEW) only when the request reserves space; after
// ABM_SETPOS it moves the window inward by what the system granted.
std::unique_ptr<IAppBar> makeWin32AppBar();

}  // namespace wabar::host
