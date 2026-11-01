#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QSize>
#include <QString>
#include <QWaitCondition>
#include <optional>

namespace wabar::host {

// Downscaled copies of image files, for a gallery that would otherwise decode full-size images
// (a 4K PNG, a 60 MB JPEG) on every open. Pure Qt Gui; the QML side is in qml/thumbnails.h.
//
// get() answers from, in order: an in-memory LRU, a file cache under `root`, a fresh decode with
// QImageReader::setScaledSize (JPEG decodes scaled; PNG decodes whole, which is why this never runs
// on the GUI thread). A fresh decode is written to the file cache. The cache key is the SHA-1 of
// the normalised path, the width, the source's modification time and size, so a changed source
// misses and the old entry ages out. A cache file that does not decode is deleted and rebuilt.
//
// The aspect ratio is kept and a source narrower than `width` is not enlarged.
//
// Threading: every method is thread safe and blocks. Two threads asking for the same entry decode
// it once; the second waits and then reads the cache. An empty `root` keeps the memory cache only.
class ThumbnailStore {
 public:
  static constexpr int kDefaultWidth = 640;
  static constexpr int kMinWidth = 16;
  static constexpr int kMaxWidth = 4096;
  static constexpr qint64 kMaxDiskBytes = 200LL * 1024 * 1024;
  static constexpr int kMaxDiskFiles = 2000;
  // One store serves the whole process (qml/thumbnails.cpp); the WebP disk cache is cheap to
  // read back, so memory holds about a screenful of gallery thumbnails.
  static constexpr qint64 kMemoryBytes = 8LL * 1024 * 1024;

  explicit ThumbnailStore(QString root);

  const QString& root() const { return root_; }
  // "webp" when the plugin is present, else "png".
  const QString& extension() const { return extension_; }

  // Width limited to [kMinWidth, kMaxWidth]; 0 or less means kDefaultWidth.
  static int clampWidth(int width);
  // Turns an id from an image URL (a path or a file: URL, percent-encoded or not) into an absolute
  // local path. "" when it names no existing file.
  static QString resolveSource(const QString& id);

  // The thumbnail of `path`, or a null image when the file is missing or not an image.
  QImage get(const QString& path, int width);
  // Makes sure the thumbnail is in the file cache. True when it is (or, without a cache folder,
  // when it could be made).
  bool warm(const QString& path, int width);

  // The cache file of `path` at `width`, "" when the source does not exist or the cache is off.
  QString cacheFile(const QString& path, int width) const;

  // Deletes the least recently used files until at most `maxFiles` files and `maxBytes` remain,
  // and temporary files left by a crashed writer. Returns the number of files deleted.
  int prune(int maxFiles = kMaxDiskFiles, qint64 maxBytes = kMaxDiskBytes);

 private:
  struct Key {
    QString hash;  // hex SHA-1
    QString source;
    int width = 0;
  };
  std::optional<Key> keyFor(const QString& path, int width) const;
  QString fileFor(const Key& key) const;
  QImage readCache(const QString& file) const;
  QImage decode(const Key& key) const;
  bool store(const QString& file, const QImage& image) const;
  QImage produce(const Key& key, bool wantImage, bool* ok);
  // True when the caller now owns the entry's production. False after waiting for another
  // producer: the caller re-reads the cache.
  bool claim(const QString& hash);
  void release(const QString& hash);

  QString root_;
  QString extension_;
  QMutex memoryMutex_;
  QCache<QString, QImage> memory_;
  QMutex flightMutex_;
  QWaitCondition flightDone_;
  QSet<QString> inFlight_;
};

}  // namespace wabar::host
