#include "host/region_math.h"

#include <cmath>

namespace wabar::host {

namespace {

bool shapeContains(const RegionNode& node, const QPointF& p) {
  const QRectF& r = node.rect;
  if (!(r.width() > 0) || !(r.height() > 0)) return false;
  if (node.shape == RegionShapeKind::Rect) {
    return p.x() >= r.left() && p.x() < r.right() && p.y() >= r.top() && p.y() < r.bottom();
  }
  const qreal dx = (p.x() - r.center().x()) / (r.width() / 2);
  const qreal dy = (p.y() - r.center().y()) / (r.height() / 2);
  return dx * dx + dy * dy <= 1.0;
}

}  // namespace

bool regionContains(const RegionNode& root, const QPointF& point) {
  bool inside = shapeContains(root, point);
  for (const RegionNode& child : root.children) {
    const bool in = regionContains(child, point);
    switch (child.op) {
      case RegionOp::Combine:
        inside = inside || in;
        break;
      case RegionOp::Intersect:
        inside = inside && in;
        break;
      case RegionOp::Xor:
        inside = inside != in;
        break;
      case RegionOp::Subtract:
        inside = inside && !in;
        break;
    }
  }
  return inside;
}

bool regionContainsPadded(const RegionNode& root, const QPointF& point, qreal pad) {
  if (regionContains(root, point)) return true;
  if (!(pad > 0)) return false;
  const qreal d = pad * 0.70710678118654752;
  const QPointF offsets[] = {{pad, 0}, {-pad, 0}, {0, pad}, {0, -pad},
                             {d, d},   {d, -d},   {-d, d},  {-d, -d}};
  for (const QPointF& o : offsets) {
    if (regionContains(root, point + o)) return true;
  }
  return false;
}

bool maskAcceptsInput(const std::optional<RegionNode>& mask, const QPointF& point, qreal pad) {
  if (!mask) return true;
  return regionContainsPadded(*mask, point, pad);
}

QPointF cursorInWindow(const QPoint& physical, const QPoint& screenOrigin, qreal dpr,
                       const QPointF& windowTopLeft) {
  const qreal scale = dpr > 0 ? dpr : 1.0;
  const QPointF logical = QPointF(screenOrigin) + QPointF(physical - screenOrigin) / scale;
  return logical - windowTopLeft;
}

}  // namespace wabar::host
