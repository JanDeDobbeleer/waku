#include "features/wallpapers/wallpaper_layout.h"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <set>

namespace wabar::wallpapers {
namespace {

QPixmap scaledTo(const QPixmap& px, int w, int h) {
  return px.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QPixmap applyTranscodeCap(const QPixmap& px, const std::optional<std::pair<int, int>>& limits) {
  const QSize size = transcodeSize(px.width(), px.height(), limits);
  if (size == px.size()) return px;
  return scaledTo(px, size.width(), size.height());
}

// Scale to cover or contain w x h, rounding the way Windows does: ratio, max for cover and min
// for contain, then half a pixel added before truncating.
QPixmap coverOrContain(const QPixmap& px, int w, int h, bool cover) {
  const double rw = static_cast<double>(w) / px.width();
  const double rh = static_cast<double>(h) / px.height();
  const double s = cover ? std::max(rw, rh) : std::min(rw, rh);
  return scaledTo(px, static_cast<int>(px.width() * s + 0.5),
                  static_cast<int>(px.height() * s + 0.5));
}

// One image across the whole virtual desktop; each monitor clips its part. `cover` is false only
// for an autospanned fit, which keeps containing and anchors at a half instead of a third.
std::vector<ScaledImage> scaleSpan(const QPixmap& px, const std::vector<ScreenArea>& areas, int vw,
                                   int vh, bool cover) {
  const QPixmap scaled = coverOrContain(px, vw, vh, cover);
  const int ox = truncDiv(vw - scaled.width(), 2);
  const int oy = truncDiv(vh - scaled.height(), cover ? 3 : 2);
  std::vector<ScaledImage> result;
  for (const ScreenArea& a : areas) result.push_back({scaled, ox - a.x, oy - a.y});
  return result;
}

// Windows never scales a tiled wallpaper, and the grid runs across the whole virtual desktop.
std::vector<ScaledImage> scaleTile(const QPixmap& px, const std::vector<ScreenArea>& areas, int vw,
                                   int vh, double dpr, const QColor& background) {
  std::vector<ScaledImage> result;
  const int iw = px.width();
  const int ih = px.height();
  if (iw <= 0 || ih <= 0) {
    for (size_t i = 0; i < areas.size(); ++i) result.push_back({px, 0, 0});
    return result;
  }
  QPixmap source(px);
  source.setDevicePixelRatio(1.0);
  QPixmap tiled(vw, vh);
  tiled.fill(background);
  QPainter painter(&tiled);
  for (int ty = 0; ty < vh; ty += ih)
    for (int tx = 0; tx < vw; tx += iw) painter.drawPixmap(tx, ty, source);
  painter.end();
  tiled.setDevicePixelRatio(dpr);
  for (const ScreenArea& a : areas) result.push_back({tiled, -a.x, -a.y});
  return result;
}

// One monitor: centre, stretch, fit or fill. Fill anchors at a third of the vertical overflow.
ScaledImage scaleForScreen(const QPixmap& px, int sw, int sh, WallpaperFit fit) {
  if (fit == WallpaperFit::Stretch) return {scaledTo(px, sw, sh), 0, 0};
  QPixmap scaled = px;
  if (fit == WallpaperFit::Fill || fit == WallpaperFit::Fit)
    scaled = coverOrContain(px, sw, sh, fit == WallpaperFit::Fill);
  const int ox = truncDiv(sw - scaled.width(), 2);
  const int oy = truncDiv(sh - scaled.height(), fit == WallpaperFit::Fill ? 3 : 2);
  return {scaled, ox, oy};
}

QPainterPath rectPath(int dx, int dy, int dw, int dh) {
  QPainterPath path;
  path.addRect(QRectF(dx, dy, dw, dh));
  return path;
}

QPainterPath clipCircle(int dx, int dy, int dw, int dh, double t) {
  const int cx = dx + dw / 2;
  const int cy = dy + dh / 2;
  const double r = std::max(
      1.0,
      std::max({std::hypot(cx - dx, cy - dy), std::hypot(cx - (dx + dw), cy - dy),
                std::hypot(cx - dx, cy - (dy + dh)), std::hypot(cx - (dx + dw), cy - (dy + dh))}) *
          t);
  QPainterPath path;
  path.addEllipse(QRectF(cx - r, cy - r, 2 * r, 2 * r));
  return path;
}

QPainterPath clipDiamond(int dx, int dy, int dw, int dh, double t) {
  const int cx = dx + dw / 2;
  const int cy = dy + dh / 2;
  const int maxR =
      std::max({std::abs(cx - dx) + std::abs(cy - dy), std::abs(cx - (dx + dw)) + std::abs(cy - dy),
                std::abs(cx - dx) + std::abs(cy - (dy + dh)),
                std::abs(cx - (dx + dw)) + std::abs(cy - (dy + dh))});
  const double rr = std::max(1.0, static_cast<double>(maxR) * t);
  QPainterPath path;
  path.addPolygon(QPolygonF(
      {QPointF(cx, cy - rr), QPointF(cx + rr, cy), QPointF(cx, cy + rr), QPointF(cx - rr, cy)}));
  return path;
}

QPainterPath clipSplit(int dx, int dy, int dw, int dh, double t) {
  const int half = static_cast<int>(dh / 2.0 * t);
  const int band = std::max(1, half);
  QPainterPath top;
  top.addRect(QRectF(dx, dy, dw, band));
  QPainterPath bottom;
  bottom.addRect(QRectF(dx, dy + dh - band, dw, band));
  return top.united(bottom);
}

QPainterPath clipSlideTop(int dx, int dy, int dw, int dh, double t) {
  const int cx = dx + dw / 2;
  const double maxR = std::hypot(dw / 2.0, static_cast<double>(dh));
  const double r = std::max(1.0, maxR * t);
  QPainterPath circle;
  circle.addEllipse(QRectF(cx - r, dy - r, 2 * r, 2 * r));
  return circle.intersected(rectPath(dx, dy, dw, dh));
}

}  // namespace

DesktopSettings readDesktopSettings() {
  DesktopSettings settings;
  settings.fit = readWallpaperFit();
  const std::uint32_t rgb = readDesktopBackgroundRgb();
  settings.background = QColor((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
  settings.transcodeLimits = readTranscodeLimits();
  settings.panoramaThreshold = readPanoramaThreshold(true);
  return settings;
}

int roundHalfEven(double value) { return static_cast<int>(std::nearbyint(value)); }

int truncDiv(int a, int b) { return a / b; }  // C++ already truncates toward zero

QSize transcodeSize(int iw, int ih, const std::optional<std::pair<int, int>>& limits) {
  if (!limits || iw <= 0 || ih <= 0) return QSize(iw, ih);
  const auto [maxVirtualDesktop, maxMonitor] = *limits;
  const double aspect = static_cast<double>(iw) / ih;
  int limit = static_cast<int>(std::max(maxMonitor * aspect, maxMonitor / aspect));
  limit = std::max(maxVirtualDesktop, limit);
  if (iw <= limit && ih <= limit) return QSize(iw, ih);
  // Both sides round down, never to nearest: 3900x3860 comes out 3879x3839.
  const int w =
      std::max(1, std::min(limit, static_cast<int>(static_cast<double>(limit) / ih * iw)));
  const int h =
      std::max(1, std::min(limit, static_cast<int>(static_cast<double>(limit) / iw * ih)));
  return QSize(w, h);
}

bool monitorsTileRectangle(const std::vector<ScreenArea>& areas) {
  if (areas.empty()) return false;
  std::set<std::pair<int, int>> sizes;
  for (const ScreenArea& a : areas) sizes.insert({a.w, a.h});
  if (sizes.size() != 1) return false;
  int left = areas.front().x;
  int top = areas.front().y;
  int right = left;
  int bottom = top;
  long long covered = 0;
  for (const ScreenArea& a : areas) {
    left = std::min(left, a.x);
    top = std::min(top, a.y);
    right = std::max(right, a.x + a.w);
    bottom = std::max(bottom, a.y + a.h);
    covered += static_cast<long long>(a.w) * a.h;
  }
  return covered == static_cast<long long>(right - left) * (bottom - top);
}

bool wantsAutospan(QSize image, const std::vector<ScreenArea>& areas, double threshold) {
  if (areas.size() < 2 || image.isEmpty() || image.height() <= 0) return false;
  if (!monitorsTileRectangle(areas)) return false;
  if (image.height() > image.width()) return false;
  return static_cast<double>(image.width()) / image.height() > threshold;
}

std::vector<ScaledImage> layoutForScreens(const QPixmap& source, std::vector<ScreenArea> areas,
                                          double dpr, const DesktopSettings& settings,
                                          QSize fallbackSize) {
  if (source.isNull()) return {};
  // Lay out what Windows lays out: its transcoded copy, not the file.
  QPixmap px = applyTranscodeCap(source, settings.transcodeLimits);
  if (dpr != 1.0) {
    for (ScreenArea& a : areas) {
      a.x = roundHalfEven(a.x * dpr);
      a.y = roundHalfEven(a.y * dpr);
      a.w = roundHalfEven(a.w * dpr);
      a.h = roundHalfEven(a.h * dpr);
    }
    px.setDevicePixelRatio(dpr);
  }
  int vw = roundHalfEven(fallbackSize.width() * dpr);
  int vh = roundHalfEven(fallbackSize.height() * dpr);
  if (!areas.empty()) {
    vw = vh = 0;
    for (const ScreenArea& a : areas) {
      vw = std::max(vw, a.x + a.w);
      vh = std::max(vh, a.y + a.h);
    }
  }

  // Only span and tile are laid out against the virtual desktop; every other mode is per monitor.
  if (settings.fit == WallpaperFit::Span) return scaleSpan(px, areas, vw, vh, true);
  if (settings.fit == WallpaperFit::Tile)
    return scaleTile(px, areas, vw, vh, dpr, settings.background);
  if ((settings.fit == WallpaperFit::Fill || settings.fit == WallpaperFit::Fit) &&
      wantsAutospan(px.size(), areas, settings.panoramaThreshold)) {
    // Windows rewrites the position to span but keeps the original cover/contain and anchor.
    return scaleSpan(px, areas, vw, vh, settings.fit == WallpaperFit::Fill);
  }
  std::vector<ScaledImage> result;
  for (const ScreenArea& a : areas) result.push_back(scaleForScreen(px, a.w, a.h, settings.fit));
  return result;
}

QPainterPath revealPath(const QString& animation, int dx, int dy, int dw, int dh, double t) {
  if (t >= 1.0) return rectPath(dx, dy, dw, dh);
  if (animation == QLatin1String("circle")) return clipCircle(dx, dy, dw, dh, t);
  if (animation == QLatin1String("slide_top")) return clipSlideTop(dx, dy, dw, dh, t);
  if (animation == QLatin1String("diamond")) return clipDiamond(dx, dy, dw, dh, t);
  if (animation == QLatin1String("split")) return clipSplit(dx, dy, dw, dh, t);
  return rectPath(dx, dy, dw, dh);
}

}  // namespace wabar::wallpapers
