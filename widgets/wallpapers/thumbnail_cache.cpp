#include "widgets/wallpapers/thumbnail_cache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>
#include <algorithm>
#include <tuple>
#include <vector>

#include "widgets/wallpapers/wallpaper_files.h"

namespace yasb::widgets {
namespace {

// Bump when the thumbnail pixels change meaning. Python's cache hashes differently, so the two
// builds never read each other's files even in the same folder.
constexpr int kCacheVersion = 1;

}  // namespace

ThumbnailCache::ThumbnailCache(QString folder) : folder_(std::move(folder)) {}

QString ThumbnailCache::key(const QString& imagePath, int width, int height) const {
  const QFileInfo info(imagePath);
  if (!info.exists()) return {};
  const QString identity = QStringLiteral("native%1|%2|%3|%4|%5x%6")
                               .arg(kCacheVersion)
                               .arg(normalizedPathKey(imagePath))
                               .arg(info.lastModified().toMSecsSinceEpoch())
                               .arg(info.size())
                               .arg(width)
                               .arg(height);
  const QByteArray hash =
      QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex().left(32);
  return QString::fromLatin1(hash) + QStringLiteral(".png");
}

std::optional<QImage> ThumbnailCache::load(const QString& name) const {
  QImage image(QDir(folder_).filePath(name));
  if (image.isNull()) return std::nullopt;
  return image;
}

void ThumbnailCache::save(const QString& name, const QImage& image) const {
  const QString path = QDir(folder_).filePath(name);
  const QString partial = QStringLiteral("%1.%2.tmp")
                              .arg(path)
                              .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
  if (QDir().mkpath(folder_) && image.save(partial, "PNG")) {
    QFile::remove(path);
    if (QFile::rename(partial, path)) return;
  }
  QFile::remove(partial);
}

void ThumbnailCache::prune() const {
  struct Entry {
    qint64 modified;
    qint64 size;
    QString path;
  };
  std::vector<Entry> entries;
  const QFileInfoList list = QDir(folder_).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
  for (const QFileInfo& info : list) {
    entries.push_back({info.lastModified().toSecsSinceEpoch(), info.size(), info.filePath()});
  }
  std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
    return std::tie(a.modified, a.size, a.path) < std::tie(b.modified, b.size, b.path);
  });
  qint64 total = 0;
  for (const Entry& entry : entries) total += entry.size;
  const qint64 expired = QDateTime::currentSecsSinceEpoch() - kMaxAgeSeconds;
  for (const Entry& entry : entries) {
    if (entry.modified >= expired && total <= kMaxBytes) break;
    if (QFile::remove(entry.path)) total -= entry.size;
  }
}

}  // namespace yasb::widgets
