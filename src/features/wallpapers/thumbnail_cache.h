#pragma once

#include <QImage>
#include <QString>
#include <optional>

namespace wabar::wallpapers {

// Port of services/wallpapers/thumbnails.py: PNG thumbnails keyed by file identity and size, kept
// under %LOCALAPPDATA%\wabar\wallpaper_thumbnails (or `folder`). Every method may run on any
// thread; the files are the only shared state.
class ThumbnailCache {
 public:
  static constexpr qint64 kMaxBytes = 100LL * 1024 * 1024;
  static constexpr qint64 kMaxAgeSeconds = 30LL * 24 * 60 * 60;

  explicit ThumbnailCache(QString folder);

  const QString& folder() const { return folder_; }

  // Cache file name for `imagePath` drawn at width x height pixels, empty if the image is gone.
  QString key(const QString& imagePath, int width, int height) const;
  std::optional<QImage> load(const QString& name) const;
  void save(const QString& name, const QImage& image) const;
  // Deletes thumbnails older than kMaxAgeSeconds, then the oldest ones until the rest fit in
  // kMaxBytes.
  void prune() const;

 private:
  QString folder_;
};

}  // namespace wabar::wallpapers
