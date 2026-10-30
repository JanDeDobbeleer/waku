#include "host/popup_placement.h"

#include <algorithm>

namespace wabar::host {

namespace {

// `value` limited to [low, high]; when the range is empty (the popup is bigger than the room),
// the low end wins so the top-left corner stays visible.
qreal clampInto(qreal value, qreal low, qreal high) {
  if (high < low) return low;
  return std::clamp(value, low, high);
}

// The start of a popup of `length` centred on `center`, kept inside [low, high].
qreal slide(qreal center, qreal length, qreal low, qreal high) {
  return clampInto(center - length / 2, low, high);
}

// The start of a popup on the preferred side of the anchor: `before` puts it left of or above the
// anchor, `after` right of or below it. It flips to the other side when the preferred one has no
// room, and is clamped into [low, high] when neither fits.
qreal pick(bool preferBefore, qreal before, qreal after, qreal low, qreal high) {
  const bool fitsBefore = before >= low;
  const bool fitsAfter = after <= high;
  if (preferBefore) return fitsBefore ? before : (fitsAfter ? after : clampInto(before, low, high));
  return fitsAfter ? after : (fitsBefore ? before : clampInto(after, low, high));
}

}  // namespace

QPointF placePopup(const QRectF& anchor, const QSizeF& size, const QRectF& screen, PopupEdge edge,
                   qreal gap, qreal margin) {
  const qreal minX = screen.left() + margin;
  const qreal maxX = screen.right() - size.width() - margin;
  const qreal minY = screen.top() + margin;
  const qreal maxY = screen.bottom() - size.height() - margin;

  if (edge == PopupEdge::Left || edge == PopupEdge::Right) {
    const qreal y = slide(anchor.center().y(), size.height(), minY, maxY);
    const qreal x = pick(edge == PopupEdge::Left, anchor.left() - gap - size.width(),
                         anchor.right() + gap, minX, maxX);
    return {x, y};
  }
  const qreal x = slide(anchor.center().x(), size.width(), minX, maxX);
  const qreal y = pick(edge == PopupEdge::Above, anchor.top() - gap - size.height(),
                       anchor.bottom() + gap, minY, maxY);
  return {x, y};
}

PopupEdge popupEdgeForDock(std::optional<AppBarEdge> edge) {
  if (!edge) return PopupEdge::Below;
  switch (*edge) {
    case AppBarEdge::Top:
      return PopupEdge::Below;
    case AppBarEdge::Bottom:
      return PopupEdge::Above;
    case AppBarEdge::Left:
      return PopupEdge::Right;
    case AppBarEdge::Right:
      return PopupEdge::Left;
  }
  return PopupEdge::Below;
}

QRectF panelAnchorRect(const QRectF& item, const QRectF& panel, bool vertical) {
  if (vertical) return QRectF(panel.x(), item.y(), panel.width(), item.height());
  return QRectF(item.x(), panel.y(), item.width(), panel.height());
}

}  // namespace wabar::host
