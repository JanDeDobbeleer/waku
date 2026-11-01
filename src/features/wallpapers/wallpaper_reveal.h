#pragma once

#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <vector>

namespace wabar::wallpapers {

// The reveal of the transition as plain geometry, for the scene graph item: the shapes of
// `revealPath` (wallpaper_layout.h) as convex polygons, which the item draws as triangle fans.
// UI-free so tests compare it with the reference path and the reference renderer.

// True for "fade" and for any name the reveal shapes do not know (they cross-fade).
bool isFadeAnimation(const QString& animation);

// Intersection of a convex polygon with a rectangle (Sutherland-Hodgman); empty when they are
// disjoint. The result is convex again.
QPolygonF clipConvex(const QPolygonF& polygon, const QRectF& rect);

// Area of a simple polygon.
double polygonArea(const QPolygonF& polygon);

// The reveal shape of `animation` at progress `t` inside the monitor (dx, dy, dw, dh), as disjoint
// convex pieces already clipped to the monitor: what `revealPath` covers. A circle is a polygon
// whose edge stays within 0.2 px of the true circle. Empty for fade shapes.
std::vector<QPolygonF> revealPieces(const QString& animation, int dx, int dy, int dw, int dh,
                                    double t);

// What one monitor draws at progress `t`, in the logical pixels of the overlay.
struct AreaFrame {
  // The old image is drawn (until the reveal is complete), over `oldRect`: the old image's
  // rectangle clipped to the monitor. Empty when the image misses the monitor.
  bool showOld = false;
  QRectF oldRect;
  // Background colour, drawn first under the new image: the whole revealed shape. Empty for a fade.
  std::vector<QPolygonF> fill;
  // The new image, drawn over `fill`: the revealed shape clipped to the new image's rectangle.
  // A fade has one piece, that rectangle clipped to the monitor.
  std::vector<QPolygonF> fresh;
  // Opacity of `fresh`: `t` for a fade, 1 for the shapes.
  double freshOpacity = 1.0;
};

// `oldImage` and `newImage` are the logical rectangles the images are drawn at (offset plus size
// over the device pixel ratio); `monitor` is the area. Mirrors `paintWallpaperFrame`.
AreaFrame computeAreaFrame(const QRectF& monitor, const QRectF& oldImage, const QRectF& newImage,
                           const QString& animation, double t);

// Number of triangles a convex polygon becomes as a fan.
int fanTriangleCount(const QPolygonF& polygon);

}  // namespace wabar::wallpapers
