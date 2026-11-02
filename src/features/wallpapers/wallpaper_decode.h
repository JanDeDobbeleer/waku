#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <optional>
#include <utility>

#include "features/wallpapers/win32/workerw.h"

namespace waku::wallpapers {

// How large a wallpaper has to be decoded for the transition. The layout scales every image to the
// screen anyway, so the full-size picture (33 MB for 4K) never needs to exist.
struct DecodeBudget {
  // The virtual desktop in device pixels; empty means "no limit".
  QSize target;
  WallpaperFit fit = WallpaperFit::Fill;
  // Windows' own cap on the transcoded copy; the layout applies it first (see transcodeSize).
  std::optional<std::pair<int, int>> transcodeLimits;
};

// The size to decode an `image`-sized picture at: the size the layout will use for it, never larger
// than the picture and never smaller than needed to cover `budget.target`. Tile and centre never
// scale, so they only get the transcode cap. Aspect ratio is kept.
QSize decodeSize(QSize image, const DecodeBudget& budget);

// Reads `path` scaled to decodeSize while decoding, so the full-size image is never held. A null
// image means the file could not be read.
QImage decodeWallpaper(const QString& path, const DecodeBudget& budget, bool autoTransform);

}  // namespace waku::wallpapers
