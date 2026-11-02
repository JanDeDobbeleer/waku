#include "features/wallpapers/wallpaper_reveal.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace waku::wallpapers {
namespace {

constexpr double kPi = 3.14159265358979323846;
// How far the polygon edge of a circle may lie inside the true circle, in pixels.
constexpr double kCircleSagitta = 0.2;

QPolygonF rectPolygon(const QRectF& r) {
  return QPolygonF({r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()});
}

QPolygonF circlePolygon(double cx, double cy, double r) {
  const double step = std::acos(std::clamp(1.0 - kCircleSagitta / r, -1.0, 1.0));
  const int n = std::clamp(static_cast<int>(std::ceil(kPi / std::max(step, 1e-3))), 16, 512);
  QPolygonF polygon;
  polygon.reserve(n);
  for (int i = 0; i < n; ++i) {
    const double angle = 2.0 * kPi * i / n;
    polygon << QPointF(cx + r * std::cos(angle), cy + r * std::sin(angle));
  }
  return polygon;
}

// Keeps the part of `in` where side(p) >= 0 for one edge of the rectangle.
template <typename Side>
QPolygonF clipAgainst(const QPolygonF& in, Side side) {
  QPolygonF out;
  const int n = in.size();
  for (int i = 0; i < n; ++i) {
    const QPointF& a = in[i];
    const QPointF& b = in[(i + 1) % n];
    const double da = side(a);
    const double db = side(b);
    if (da >= 0) out << a;
    if ((da >= 0) != (db >= 0)) {
      const double k = da / (da - db);
      out << QPointF(a.x() + (b.x() - a.x()) * k, a.y() + (b.y() - a.y()) * k);
    }
  }
  return out;
}

bool usable(const QPolygonF& polygon) {
  return polygon.size() >= 3 && std::abs(polygonArea(polygon)) > 1e-9;
}

}  // namespace

bool isFadeAnimation(const QString& animation) {
  return animation != QLatin1String("circle") && animation != QLatin1String("slide_top") &&
         animation != QLatin1String("diamond") && animation != QLatin1String("split");
}

QPolygonF clipConvex(const QPolygonF& polygon, const QRectF& rect) {
  if (polygon.size() < 3 || rect.isEmpty()) return {};
  QPolygonF p = polygon;
  p = clipAgainst(p, [&](const QPointF& q) { return q.x() - rect.left(); });
  if (p.isEmpty()) return {};
  p = clipAgainst(p, [&](const QPointF& q) { return rect.right() - q.x(); });
  if (p.isEmpty()) return {};
  p = clipAgainst(p, [&](const QPointF& q) { return q.y() - rect.top(); });
  if (p.isEmpty()) return {};
  p = clipAgainst(p, [&](const QPointF& q) { return rect.bottom() - q.y(); });
  return usable(p) ? p : QPolygonF();
}

double polygonArea(const QPolygonF& polygon) {
  double twice = 0.0;
  const int n = polygon.size();
  for (int i = 0; i < n; ++i) {
    const QPointF& a = polygon[i];
    const QPointF& b = polygon[(i + 1) % n];
    twice += a.x() * b.y() - b.x() * a.y();
  }
  return std::abs(twice) / 2.0;
}

std::vector<QPolygonF> revealPieces(const QString& animation, int dx, int dy, int dw, int dh,
                                    double t) {
  const QRectF monitor(dx, dy, dw, dh);
  std::vector<QPolygonF> pieces;
  auto add = [&](const QPolygonF& polygon) {
    const QPolygonF clipped = clipConvex(polygon, monitor);
    if (!clipped.isEmpty()) pieces.push_back(clipped);
  };
  if (isFadeAnimation(animation)) return pieces;
  if (t >= 1.0) {
    add(rectPolygon(monitor));
    return pieces;
  }

  const int cx = dx + dw / 2;
  const int cy = dy + dh / 2;
  if (animation == QLatin1String("circle")) {
    const double r =
        std::max(1.0, std::max({std::hypot(cx - dx, cy - dy), std::hypot(cx - (dx + dw), cy - dy),
                                std::hypot(cx - dx, cy - (dy + dh)),
                                std::hypot(cx - (dx + dw), cy - (dy + dh))}) *
                          t);
    add(circlePolygon(cx, cy, r));
  } else if (animation == QLatin1String("slide_top")) {
    const double maxR = std::hypot(dw / 2.0, static_cast<double>(dh));
    add(circlePolygon(cx, dy, std::max(1.0, maxR * t)));
  } else if (animation == QLatin1String("diamond")) {
    const int maxR = std::max({std::abs(cx - dx) + std::abs(cy - dy),
                               std::abs(cx - (dx + dw)) + std::abs(cy - dy),
                               std::abs(cx - dx) + std::abs(cy - (dy + dh)),
                               std::abs(cx - (dx + dw)) + std::abs(cy - (dy + dh))});
    const double rr = std::max(1.0, static_cast<double>(maxR) * t);
    add(QPolygonF(
        {QPointF(cx, cy - rr), QPointF(cx + rr, cy), QPointF(cx, cy + rr), QPointF(cx - rr, cy)}));
  } else {  // split
    const int band = std::max(1, static_cast<int>(dh / 2.0 * t));
    add(rectPolygon(QRectF(dx, dy, dw, band)));
    add(rectPolygon(QRectF(dx, dy + dh - band, dw, band)));
  }
  return pieces;
}

AreaFrame computeAreaFrame(const QRectF& monitor, const QRectF& oldImage, const QRectF& newImage,
                           const QString& animation, double t) {
  AreaFrame frame;
  frame.showOld = t < 1.0;
  frame.oldRect = oldImage.intersected(monitor);
  if (!frame.oldRect.isValid() || frame.oldRect.isEmpty()) frame.oldRect = QRectF();

  const QRectF visible = newImage.intersected(monitor);
  if (isFadeAnimation(animation)) {
    frame.freshOpacity = std::clamp(t, 0.0, 1.0);
    if (visible.isValid() && !visible.isEmpty()) frame.fresh.push_back(rectPolygon(visible));
    return frame;
  }

  // Integer monitor geometry, as revealPath takes it.
  frame.fill =
      revealPieces(animation, static_cast<int>(monitor.x()), static_cast<int>(monitor.y()),
                   static_cast<int>(monitor.width()), static_cast<int>(monitor.height()), t);
  for (const QPolygonF& piece : frame.fill) {
    const QPolygonF part = clipConvex(piece, visible);
    if (!part.isEmpty()) frame.fresh.push_back(part);
  }
  return frame;
}

int fanTriangleCount(const QPolygonF& polygon) {
  return std::max(0, static_cast<int>(polygon.size()) - 2);
}

}  // namespace waku::wallpapers
