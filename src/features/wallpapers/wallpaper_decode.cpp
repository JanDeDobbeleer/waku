#include "features/wallpapers/wallpaper_decode.h"

#include <QImageReader>
#include <algorithm>

#include "features/wallpapers/wallpaper_layout.h"

namespace wabar::wallpapers {

QSize decodeSize(QSize image, const DecodeBudget& budget) {
  if (image.width() <= 0 || image.height() <= 0) return image;
  const QSize base = transcodeSize(image.width(), image.height(), budget.transcodeLimits);
  if (budget.fit == WallpaperFit::Tile || budget.fit == WallpaperFit::Center) return base;
  if (budget.target.width() <= 0 || budget.target.height() <= 0) return base;
  // Cover of the whole target is enough for every fit: contain and per-monitor layouts need less.
  const double scale = std::max(static_cast<double>(budget.target.width()) / base.width(),
                                static_cast<double>(budget.target.height()) / base.height());
  if (scale >= 1.0) return base;
  // The rounding of the layout's own cover, so its scaling step finds the size it wants.
  return QSize(std::max(1, static_cast<int>(base.width() * scale + 0.5)),
               std::max(1, static_cast<int>(base.height() * scale + 0.5)));
}

QImage decodeWallpaper(const QString& path, const DecodeBudget& budget, bool autoTransform) {
  QImageReader reader(path);
  reader.setAutoTransform(autoTransform);
  const QSize full = reader.size();
  if (full.isValid()) {
    const QSize wanted = decodeSize(full, budget);
    if (wanted != full) reader.setScaledSize(wanted);
  }
  return reader.read();
}

}  // namespace wabar::wallpapers
