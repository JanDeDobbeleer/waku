#include "spike/bar/popup_placement.h"

#include <algorithm>

namespace tidy {

namespace {

// `value` limited to [low, high]; when the range is empty (the popup is bigger than the room),
// the low end wins so the top-left corner stays visible.
qreal clampInto(qreal value, qreal low, qreal high) {
  if (high < low) return low;
  return std::clamp(value, low, high);
}

}  // namespace

QPointF PopupPlacement::place(const QRectF& anchor, const QSizeF& size, const QRectF& screen,
                              Edge edge, qreal gap, qreal margin) {
  const qreal minX = screen.left() + margin;
  const qreal maxX = screen.right() - size.width() - margin;
  const qreal x = clampInto(anchor.center().x() - size.width() / 2, minX, maxX);

  const qreal minY = screen.top() + margin;
  const qreal maxY = screen.bottom() - size.height() - margin;
  const qreal below = anchor.bottom() + gap;
  const qreal above = anchor.top() - gap - size.height();
  const bool fitsBelow = below <= maxY;
  const bool fitsAbove = above >= minY;

  qreal y;
  if (edge == Below) {
    y = fitsBelow ? below : (fitsAbove ? above : clampInto(below, minY, maxY));
  } else {
    y = fitsAbove ? above : (fitsBelow ? below : clampInto(above, minY, maxY));
  }
  return {x, y};
}

}  // namespace tidy
