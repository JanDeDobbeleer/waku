#include "host/panel_geometry.h"

#include <algorithm>

namespace wabar::host {

namespace {

// One axis: the start and length of the window on [origin, origin + room).
std::pair<int, int> place(bool start, bool end, int marginStart, int marginEnd, int implicit,
                          int origin, int room) {
  if (start && end) return {origin + marginStart, std::max(0, room - marginStart - marginEnd)};
  if (start) return {origin + marginStart, implicit};
  if (end) return {origin + room - marginEnd - implicit, implicit};
  return {origin + (room - implicit) / 2, implicit};
}

}  // namespace

QRect panelGeometry(const PanelAnchorSet& anchors, const QMargins& margins,
                    const QSize& implicitSize, const QRect& screen) {
  const auto [x, width] = place(anchors.left, anchors.right, margins.left(), margins.right(),
                                std::max(0, implicitSize.width()), screen.x(), screen.width());
  const auto [y, height] = place(anchors.top, anchors.bottom, margins.top(), margins.bottom(),
                                 std::max(0, implicitSize.height()), screen.y(), screen.height());
  return QRect(x, y, width, height);
}

std::optional<AppBarEdge> panelAppBarEdge(const PanelAnchorSet& anchors) {
  if (anchors.top && !anchors.bottom) return AppBarEdge::Top;
  if (anchors.bottom && !anchors.top) return AppBarEdge::Bottom;
  return std::nullopt;
}

int panelReservedZone(const PanelAnchorSet& anchors, const QMargins& margins, int mode,
                      int exclusiveZone, const QSize& size) {
  constexpr int kIgnore = 0;
  constexpr int kNormal = 1;
  const std::optional<AppBarEdge> edge = panelAppBarEdge(anchors);
  if (!edge || mode == kIgnore) return 0;
  const int margin = *edge == AppBarEdge::Top ? margins.top() : margins.bottom();
  const int zone = mode == kNormal ? exclusiveZone : size.height();
  return zone > 0 ? zone + margin : 0;
}

}  // namespace wabar::host
