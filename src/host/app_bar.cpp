#include "host/app_bar.h"

#include <algorithm>
#include <cmath>

namespace waku::host {

AppBarRect physicalScreenRect(const QRect& g, double dpr) {
  const double scale = dpr > 0 ? dpr : 1.0;
  const int width = static_cast<int>(std::lround(g.width() * scale));
  const int height = static_cast<int>(std::lround(g.height() * scale));
  return {g.x(), g.y(), g.x() + width, g.y() + height};
}

namespace {

// `rect` with its thickness side set `thicknessPx` from the side `edge` names; the side on the
// screen edge stays.
AppBarRect withThickness(AppBarEdge edge, int thicknessPx, AppBarRect rect) {
  switch (edge) {
    case AppBarEdge::Left:
      rect.right = rect.left + thicknessPx;
      break;
    case AppBarEdge::Top:
      rect.bottom = rect.top + thicknessPx;
      break;
    case AppBarEdge::Right:
      rect.left = rect.right - thicknessPx;
      break;
    case AppBarEdge::Bottom:
      rect.top = rect.bottom - thicknessPx;
      break;
  }
  return rect;
}

// How far in from the screen edge the rect's edge side is.
int insetOf(AppBarEdge edge, const AppBarRect& asked, const AppBarRect& got) {
  switch (edge) {
    case AppBarEdge::Left:
      return got.left - asked.left;
    case AppBarEdge::Top:
      return got.top - asked.top;
    case AppBarEdge::Right:
      return asked.right - got.right;
    case AppBarEdge::Bottom:
      return asked.bottom - got.bottom;
  }
  return 0;
}

}  // namespace

AppBarRect computeAppBarRect(AppBarEdge edge, int thicknessPx, const AppBarRect& screen) {
  return withThickness(edge, std::max(0, thicknessPx), screen);
}

AppBarRect computeAppBarRect(AppBarEdge edge, int thickness, const QRect& g, double dpr) {
  const double scale = dpr > 0 ? dpr : 1.0;
  return computeAppBarRect(edge, static_cast<int>(std::lround(thickness * scale)),
                           physicalScreenRect(g, scale));
}

AppBarGrant negotiateAppBarRect(IAppBarSystem& system, AppBarEdge edge, int thicknessPx,
                                const AppBarRect& screen) {
  const int thickness = std::max(0, thicknessPx);
  AppBarGrant grant;
  grant.requested = computeAppBarRect(edge, thickness, screen);
  // QUERYPOS may move the edge side inward (another app bar, the taskbar); the thickness is ours,
  // so it is applied again to the rect that comes back before SETPOS.
  const AppBarRect queried = system.queryPos(edge, grant.requested);
  const AppBarRect proposed = withThickness(edge, thickness, queried);
  // SETPOS can adjust once more; what it returns is the rect the system holds for this bar.
  grant.granted = withThickness(edge, thickness, system.setPos(edge, proposed));
  grant.inset = std::max(0, insetOf(edge, grant.requested, grant.granted));
  return grant;
}

QPoint appBarWindowShift(AppBarEdge edge, int inset) {
  switch (edge) {
    case AppBarEdge::Left:
      return {inset, 0};
    case AppBarEdge::Top:
      return {0, inset};
    case AppBarEdge::Right:
      return {-inset, 0};
    case AppBarEdge::Bottom:
      return {0, -inset};
  }
  return {};
}

}  // namespace waku::host
