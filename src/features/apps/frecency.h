#pragma once

#include <QHash>
#include <QString>

namespace waku::apps {

// How often and how recently each app was launched, kept in a JSON file
// (%LOCALAPPDATA%\waku\apps_frecency.json). The score is a launch count that halves every
// `kHalfLifeDays` days; the ranking uses it only to break ties.
//
// Not thread safe: the apps service uses it on the GUI thread. An empty path keeps the data in
// memory only.
class FrecencyStore {
 public:
  static constexpr double kHalfLifeDays = 14.0;
  static constexpr int kMaxEntries = 500;

  explicit FrecencyStore(QString path = QString());

  // Reads the file; a missing or damaged file leaves the store empty.
  void load();
  // Writes the file (atomically). False when the path is empty or the write failed.
  bool save() const;

  // One more launch of `id` at `nowMs` (milliseconds since the epoch).
  void record(const QString& id, qint64 nowMs);
  // The decayed launch count of `id`, 0 when it was never launched.
  double score(const QString& id, qint64 nowMs) const;
  int size() const { return static_cast<int>(entries_.size()); }

 private:
  struct Entry {
    double count = 0;  // launches, as of `lastMs` (already decayed to that moment)
    qint64 lastMs = 0;
  };
  static double decayed(const Entry& entry, qint64 nowMs);

  QString path_;
  QHash<QString, Entry> entries_;
};

}  // namespace waku::apps
