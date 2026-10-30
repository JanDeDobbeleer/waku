#pragma once

#include <QList>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <optional>

namespace wabar::host {

// How a child region combines with what its parent has built so far (Quickshell `Intersection`).
enum class RegionOp { Combine, Intersect, Xor, Subtract };

enum class RegionShapeKind { Rect, Ellipse };

// One node of an input mask: a shape in window coordinates (logical pixels) and child nodes that
// are folded into it in order, each by its own `op`. The value form of the QML `Region`, so the
// math is testable without QML. The `op` of the root node combines the root with the window
// rectangle (see regionContainsInWindow).
struct RegionNode {
  QRectF rect;
  RegionShapeKind shape = RegionShapeKind::Rect;
  RegionOp op = RegionOp::Combine;
  QList<RegionNode> children;
};

// Whether `point` is inside the region (the shape, then each child applied to the result).
// A shape with no area contains nothing, so a `Region {}` is empty.
bool regionContains(const RegionNode& root, const QPointF& point);

// Whether `point` is inside the root region as a window's mask: the root's own `op` is applied
// against the window rectangle (0, 0, window), as Quickshell does. Combine gives the root alone;
// Intersect, Xor and Subtract give `window <op> root`. An empty `window` size means the size is
// unknown and the root `op` is ignored.
bool regionContainsInWindow(const RegionNode& root, const QPointF& point, const QSizeF& window);

// Whether any of nine samples (the point and eight points `pad` away, on the axes and the
// diagonals) is inside the region (regionContainsInWindow): the region grown by about `pad`.
// pad <= 0 tests the point.
bool regionContainsPadded(const RegionNode& root, const QPointF& point, qreal pad,
                          const QSizeF& window = QSizeF());

// The input decision for a window: no mask accepts input everywhere; a mask accepts it where the
// padded region is.
bool maskAcceptsInput(const std::optional<RegionNode>& mask, const QPointF& point, qreal pad,
                      const QSizeF& window = QSizeF());

// A cursor position in physical (native) pixels as a point in a window's logical coordinates. The
// window's screen starts at `screenOrigin` (native pixels, which Qt also uses as the logical
// origin) and has device pixel ratio `dpr`; the window is at `windowTopLeft` (logical).
QPointF cursorInWindow(const QPoint& physical, const QPoint& screenOrigin, qreal dpr,
                       const QPointF& windowTopLeft);

}  // namespace wabar::host
