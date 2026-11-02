#include "host/thumbnail_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <filesystem>
#include <system_error>
#include <vector>

namespace waku::host {
namespace {

constexpr int kQuality = 85;
// A 16000 x 9000 JPEG is 576 MB at full size. Plugins check the source size against the limit even
// when the scaled read is small, so allow large sources and refuse absurd ones.
constexpr int kAllocationLimitMb = 2048;
constexpr qint64 kTouchAfterSeconds = 60 * 60;

// Windows paths are case-insensitive: one file, one key.
QString normalised(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();
}

int imageCostKb(const QImage& image) {
  return int(std::max<qint64>(1, image.sizeInBytes() / 1024));
}

}  // namespace

ThumbnailStore::ThumbnailStore(QString root)
    : root_(std::move(root)), memory_(int(kMemoryBytes / 1024)) {
  extension_ = QImageWriter::supportedImageFormats().contains("webp") ? QStringLiteral("webp")
                                                                      : QStringLiteral("png");
  if (!root_.isEmpty()) QDir().mkpath(root_);
}

int ThumbnailStore::clampWidth(int width) {
  if (width <= 0) return kDefaultWidth;
  return std::clamp(width, kMinWidth, kMaxWidth);
}

QString ThumbnailStore::resolveSource(const QString& id) {
  auto existing = [](const QString& candidate) -> QString {
    if (candidate.isEmpty()) return {};
    QString path = candidate;
    if (path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
      path = QUrl(path).toLocalFile();
    }
    const QFileInfo info(path);
    return info.isFile() ? info.absoluteFilePath() : QString();
  };
  QString found = existing(id);
  if (found.isEmpty() && id.contains(QLatin1Char('%'))) {
    found = existing(QUrl::fromPercentEncoding(id.toUtf8()));
  }
  return found;
}

std::optional<ThumbnailStore::Key> ThumbnailStore::keyFor(const QString& path, int width) const {
  const QString source = resolveSource(path);
  if (source.isEmpty()) return std::nullopt;
  const QFileInfo info(source);
  Key key;
  key.source = source;
  key.width = clampWidth(width);
  const QString material = normalised(source) + QLatin1Char('|') + QString::number(key.width) +
                           QLatin1Char('|') +
                           QString::number(info.lastModified().toMSecsSinceEpoch()) +
                           QLatin1Char('|') + QString::number(info.size());
  key.hash = QString::fromLatin1(
      QCryptographicHash::hash(material.toUtf8(), QCryptographicHash::Sha1).toHex());
  return key;
}

QString ThumbnailStore::fileFor(const Key& key) const {
  if (root_.isEmpty()) return {};
  return root_ + QLatin1Char('/') + key.hash + QLatin1Char('.') + extension_;
}

QString ThumbnailStore::cacheFile(const QString& path, int width) const {
  const auto key = keyFor(path, width);
  return key ? fileFor(*key) : QString();
}

QImage ThumbnailStore::readCache(const QString& file) const {
  if (file.isEmpty() || !QFileInfo::exists(file)) return {};
  QImageReader reader(file);
  reader.setAllocationLimit(kAllocationLimitMb);
  QImage image = reader.read();
  if (image.isNull()) {
    // Truncated or corrupt: drop it so the caller rebuilds it.
    QFile::remove(file);
    return {};
  }
  // Keep the least-recently-used order honest without a write on every hit.
  if (QFileInfo(file).lastModified().secsTo(QDateTime::currentDateTime()) > kTouchAfterSeconds) {
    QFile touch(file);
    if (touch.open(QIODevice::ReadWrite)) {
      touch.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
    }
  }
  return image;
}

QImage ThumbnailStore::decode(const Key& key) const {
  QImageReader reader(key.source);
  reader.setAllocationLimit(kAllocationLimitMb);
  reader.setAutoTransform(false);
  const QSize original = reader.size();
  if (original.isValid() && original.width() > key.width) {
    const int height = std::max(1, int(qint64(original.height()) * key.width / original.width()));
    reader.setScaledSize(QSize(key.width, height));
  }
  QImage image = reader.read();
  if (image.isNull()) return {};
  // A plugin without scaled decode (PNG) returns the full size.
  if (image.width() > key.width) {
    image = image.scaledToWidth(key.width, Qt::SmoothTransformation);
  }
  return image;
}

bool ThumbnailStore::store(const QString& file, const QImage& image) const {
  const QString temp =
      file + QStringLiteral(".tmp") + QString::number(quintptr(QThread::currentThreadId()), 16);
  {
    QImageWriter writer(temp, extension_.toLatin1());
    writer.setQuality(kQuality);
    if (!writer.write(image)) {
      QFile::remove(temp);
      return false;
    }
  }
  // std::filesystem::rename replaces an existing target on Windows; QFile::rename does not.
  std::error_code ec;
  std::filesystem::rename(std::filesystem::path(temp.toStdWString()),
                          std::filesystem::path(file.toStdWString()), ec);
  if (ec) {
    QFile::remove(temp);
    return false;
  }
  return true;
}

bool ThumbnailStore::claim(const QString& hash) {
  QMutexLocker lock(&flightMutex_);
  bool waited = false;
  while (inFlight_.contains(hash)) {
    waited = true;
    flightDone_.wait(&flightMutex_);
  }
  if (waited) return false;
  inFlight_.insert(hash);
  return true;
}

void ThumbnailStore::release(const QString& hash) {
  QMutexLocker lock(&flightMutex_);
  inFlight_.remove(hash);
  flightDone_.wakeAll();
}

QImage ThumbnailStore::produce(const Key& key, bool wantImage, bool* ok) {
  const QString file = fileFor(key);
  QImage image = readCache(file);
  if (!image.isNull()) {
    *ok = true;
    return image;
  }
  if (!claim(key.hash)) {
    // Another thread produced it while we waited.
    image = readCache(file);
    if (!image.isNull()) {
      *ok = true;
      return image;
    }
    // It failed, or there is no disk cache: claim it ourselves once.
    if (!claim(key.hash)) {
      image = readCache(file);
      *ok = !image.isNull();
      return image;
    }
  }
  struct Releaser {
    ThumbnailStore* self;
    const QString& hash;
    ~Releaser() { self->release(hash); }
  } releaser{this, key.hash};

  image = decode(key);
  *ok = !image.isNull();
  if (image.isNull()) return {};
  if (!file.isEmpty() && !store(file, image)) {
    qWarning().noquote() << "thumbnails: cannot write" << file;
  }
  return wantImage ? image : QImage();
}

QImage ThumbnailStore::get(const QString& path, int width) {
  try {
    const auto key = keyFor(path, width);
    if (!key) return {};
    {
      QMutexLocker lock(&memoryMutex_);
      if (const QImage* hit = memory_.object(key->hash)) return *hit;
    }
    bool ok = false;
    QImage image = produce(*key, true, &ok);
    if (image.isNull()) return {};
    QMutexLocker lock(&memoryMutex_);
    memory_.insert(key->hash, new QImage(image), imageCostKb(image));
    return image;
  } catch (const std::exception& error) {
    qWarning().noquote() << "thumbnails:" << error.what();
  } catch (...) {
    qWarning() << "thumbnails: unknown error";
  }
  return {};
}

bool ThumbnailStore::warm(const QString& path, int width) {
  try {
    const auto key = keyFor(path, width);
    if (!key) return false;
    {
      QMutexLocker lock(&memoryMutex_);
      if (memory_.contains(key->hash)) return true;
    }
    if (root_.isEmpty()) return !get(path, width).isNull();
    // A cheap probe first: do not decode a cached file only to drop the pixels.
    const QString file = fileFor(*key);
    if (QFileInfo::exists(file) && QImageReader(file).size().isValid()) return true;
    bool ok = false;
    produce(*key, false, &ok);
    return ok;
  } catch (const std::exception& error) {
    qWarning().noquote() << "thumbnails:" << error.what();
  } catch (...) {
    qWarning() << "thumbnails: unknown error";
  }
  return false;
}

int ThumbnailStore::prune(int maxFiles, qint64 maxBytes) {
  if (root_.isEmpty()) return 0;
  struct Entry {
    QString path;
    qint64 size;
    QDateTime time;
  };
  std::vector<Entry> entries;
  int removed = 0;
  const QFileInfoList files = QDir(root_).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
  for (const QFileInfo& info : files) {
    if (info.fileName().contains(QStringLiteral(".tmp"))) {
      // A temp file older than a minute belongs to a crashed writer.
      if (info.lastModified().secsTo(QDateTime::currentDateTime()) > 60 &&
          QFile::remove(info.absoluteFilePath())) {
        ++removed;
      }
      continue;
    }
    entries.push_back({info.absoluteFilePath(), info.size(), info.lastModified()});
  }
  std::sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) { return a.time > b.time; });  // newest first
  qint64 total = 0;
  int count = 0;
  for (const Entry& entry : entries) {
    total += entry.size;
    ++count;
    if ((count > maxFiles || total > maxBytes) && QFile::remove(entry.path)) ++removed;
  }
  return removed;
}

}  // namespace waku::host
