#include "features/wallpapers/lock_screen.h"

#include <QFileInfo>
#include <QImageReader>
#include <QStringList>

namespace waku::wallpapers {

std::optional<QString> lockScreenSkipReason(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile()) return QStringLiteral("no such file");
  static const QStringList videos{
      QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mkv"), QStringLiteral("webm"),
      QStringLiteral("mov"), QStringLiteral("avi"), QStringLiteral("wmv")};
  const QString suffix = info.suffix().toLower();
  if (videos.contains(suffix)) return QStringLiteral("a video");
  // Only these formats animate in Qt; a still GIF or WebP is fine.
  if (suffix == QLatin1String("gif") || suffix == QLatin1String("webp")) {
    QImageReader reader(info.absoluteFilePath());
    if (reader.supportsAnimation() && reader.imageCount() > 1) {
      return QStringLiteral("an animated image");
    }
  }
  return std::nullopt;
}

bool lockScreenNeedsCopy(const QString& path) {
  static const QStringList accepted{QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                    QStringLiteral("png"), QStringLiteral("bmp")};
  return !accepted.contains(QFileInfo(path).suffix().toLower());
}

}  // namespace waku::wallpapers
