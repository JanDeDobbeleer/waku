#pragma once

#include <QHash>
#include <QMutex>
#include <QString>
#include <optional>

namespace wabar::color_scheme {

// What the colour engine learns from an image file (its seed colour and its mean luminance), kept
// across runs so a start-up does not decode the wallpaper again. The key is the file's identity:
// canonical path, modification time and size, so an edited file misses. The variant, mode and
// contrast are not part of the key: they only matter after the seed is known and that step is
// cheap.
//
// Stored as one small JSON file (at most kMaxEntries, oldest dropped), rewritten atomically after
// a change. An empty `file` keeps the values in memory only (tests, offscreen runs). A missing or
// corrupt file reads as empty. Thread safe.
class ImageStatsCache {
 public:
  static constexpr int kMaxEntries = 256;

  explicit ImageStatsCache(QString file = QString());

  // The key of `path`, "" when the file does not exist.
  static QString keyFor(const QString& path);

  std::optional<quint32> seed(const QString& key) const;
  std::optional<double> luminance(const QString& key) const;
  void setSeed(const QString& key, quint32 argb);
  void setLuminance(const QString& key, double value);

 private:
  struct Entry {
    std::optional<quint32> seed;
    std::optional<double> luminance;
    qint64 touched = 0;
  };
  void load();
  void save();  // callers hold mutex_

  QString file_;
  mutable QMutex mutex_;
  QHash<QString, Entry> entries_;
};

}  // namespace wabar::color_scheme
