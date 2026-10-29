#pragma once

#include <QColor>
#include <QPainterPath>
#include <QPixmap>
#include <QSize>
#include <QString>
#include <optional>
#include <utility>
#include <vector>

#include "features/wallpapers/win32/workerw.h"

namespace wabar::wallpapers {

// Geometry of the wallpaper transition (services/wallpapers/engine.py). The goal is to lay the new
// image out on the same pixels Windows will, so the desktop does not jump when the overlay ends.
// Everything here is pure and testable; the registry reads are gathered in DesktopSettings.

// One monitor in logical pixels with its device pixel ratio.
struct ScreenArea {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;
  double dpr = 1.0;
};

struct DesktopSettings {
  WallpaperFit fit = WallpaperFit::Fill;
  QColor background = QColor(0, 0, 0);
  // MaxVirtualDesktopDimension and MaxMonitorDimension; nullopt makes Windows skip the resize.
  std::optional<std::pair<int, int>> transcodeLimits;
  double panoramaThreshold = 2.22;
};
DesktopSettings readDesktopSettings();

// A laid-out image and where its top-left corner goes relative to the monitor, in device pixels.
struct ScaledImage {
  QPixmap pixmap;
  int offsetX = 0;
  int offsetY = 0;
};

// Python's round(): half to even.
int roundHalfEven(double value);
// C integer division, truncating toward zero (Python's // floors).
int truncDiv(int a, int b);

// The size Windows transcodes an iw x ih wallpaper to. Both sides round down.
QSize transcodeSize(int iw, int ih, const std::optional<std::pair<int, int>>& limits);

// Whether the monitors are all one size and cover their bounding box exactly.
bool monitorsTileRectangle(const std::vector<ScreenArea>& areas);
// Whether Windows turns this fit/fill into a span (landscape image wider than the threshold on a
// spannable arrangement).
bool wantsAutospan(QSize image, const std::vector<ScreenArea>& areas, double threshold);

// Lays `source` out for every monitor in `areas` (logical pixels, `dpr` applies to all).
// `fallbackSize` is the overlay size, used only when `areas` is empty.
std::vector<ScaledImage> layoutForScreens(const QPixmap& source, std::vector<ScreenArea> areas,
                                          double dpr, const DesktopSettings& settings,
                                          QSize fallbackSize);

// Reveal shape of the new wallpaper at progress t (0..1). Unknown animations reveal everything.
QPainterPath revealPath(const QString& animation, int dx, int dy, int dw, int dh, double t);

}  // namespace wabar::wallpapers
