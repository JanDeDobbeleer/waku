#include "host/panel_geometry.h"

#include <algorithm>

namespace waku::host {

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
  const bool acrossHorizontally = anchors.left == anchors.right;  // stretched or centred
  const bool acrossVertically = anchors.top == anchors.bottom;
  if (anchors.top != anchors.bottom && acrossHorizontally) {
    return anchors.top ? AppBarEdge::Top : AppBarEdge::Bottom;
  }
  if (anchors.left != anchors.right && acrossVertically) {
    return anchors.left ? AppBarEdge::Left : AppBarEdge::Right;
  }
  return std::nullopt;
}

int panelThickness(AppBarEdge edge, const QSize& size) {
  return isVerticalEdge(edge) ? size.width() : size.height();
}

bool coversScreenForFullscreen(const QRect& window, const QRect& screen, bool maximized) {
  return !screen.isEmpty() && (maximized ? window.intersects(screen) : window.contains(screen));
}

QRect panelRevealArea(AppBarEdge edge, const QRect& panel, const QRect& screen, bool revealed) {
  if (panel.isEmpty() || screen.isEmpty()) return {};
  QRect area = panel;
  switch (edge) {
    case AppBarEdge::Top:
      area.setTop(screen.top());
      if (!revealed) area.setBottom(screen.top() + 1);
      break;
    case AppBarEdge::Bottom:
      area.setBottom(screen.bottom());
      if (!revealed) area.setTop(screen.bottom() - 1);
      break;
    case AppBarEdge::Left:
      area.setLeft(screen.left());
      if (!revealed) area.setRight(screen.left() + 1);
      break;
    case AppBarEdge::Right:
      area.setRight(screen.right());
      if (!revealed) area.setLeft(screen.right() - 1);
      break;
  }
  return area.intersected(screen);
}

int panelReservedZone(const PanelAnchorSet& anchors, const QMargins& margins, int mode,
                      int exclusiveZone, const QSize& size) {
  constexpr int kIgnore = 0;
  constexpr int kNormal = 1;
  const std::optional<AppBarEdge> edge = panelAppBarEdge(anchors);
  if (!edge || mode == kIgnore) return 0;
  int margin = 0;
  switch (*edge) {
    case AppBarEdge::Left:
      margin = margins.left();
      break;
    case AppBarEdge::Top:
      margin = margins.top();
      break;
    case AppBarEdge::Right:
      margin = margins.right();
      break;
    case AppBarEdge::Bottom:
      margin = margins.bottom();
      break;
  }
  const int zone = mode == kNormal ? exclusiveZone : panelThickness(*edge, size);
  return zone > 0 ? zone + margin : 0;
}

}  // namespace waku::host
