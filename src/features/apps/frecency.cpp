#include "features/apps/frecency.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>
#include <cmath>
#include <vector>

namespace wabar::apps {

FrecencyStore::FrecencyStore(QString path) : path_(std::move(path)) {}

double FrecencyStore::decayed(const Entry& entry, qint64 nowMs) {
  const double days = std::max<double>(0, static_cast<double>(nowMs - entry.lastMs)) / 86400000.0;
  return entry.count * std::pow(0.5, days / kHalfLifeDays);
}

void FrecencyStore::load() {
  entries_.clear();
  if (path_.isEmpty()) return;
  QFile file(path_);
  if (!file.open(QIODevice::ReadOnly)) return;
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return;
  const QJsonObject apps = document.object().value(QStringLiteral("apps")).toObject();
  for (auto it = apps.begin(); it != apps.end(); ++it) {
    const QJsonObject item = it.value().toObject();
    const double count = item.value(QStringLiteral("count")).toDouble();
    if (!(count > 0)) continue;
    entries_.insert(
        it.key(), Entry{count, static_cast<qint64>(item.value(QStringLiteral("last")).toDouble())});
  }
}

bool FrecencyStore::save() const {
  if (path_.isEmpty()) return false;
  // Keep the best kMaxEntries so the file does not grow with every app ever launched.
  std::vector<std::pair<QString, Entry>> items;
  for (auto it = entries_.begin(); it != entries_.end(); ++it)
    items.emplace_back(it.key(), it.value());
  if (static_cast<int>(items.size()) > kMaxEntries) {
    qint64 newest = 0;
    for (const auto& item : items) newest = std::max(newest, item.second.lastMs);
    std::sort(items.begin(), items.end(), [&](const auto& a, const auto& b) {
      return decayed(a.second, newest) > decayed(b.second, newest);
    });
    items.resize(kMaxEntries);
  }
  QJsonObject apps;
  for (const auto& item : items) {
    QJsonObject entry;
    entry.insert(QStringLiteral("count"), item.second.count);
    entry.insert(QStringLiteral("last"), static_cast<double>(item.second.lastMs));
    apps.insert(item.first, entry);
  }
  QJsonObject root;
  root.insert(QStringLiteral("version"), 1);
  root.insert(QStringLiteral("apps"), apps);
  QSaveFile file(path_);
  if (!file.open(QIODevice::WriteOnly)) return false;
  file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
  return file.commit();
}

void FrecencyStore::record(const QString& id, qint64 nowMs) {
  if (id.isEmpty()) return;
  Entry& entry = entries_[id];
  entry.count = (entry.lastMs > 0 ? decayed(entry, nowMs) : 0) + 1;
  entry.lastMs = nowMs;
}

double FrecencyStore::score(const QString& id, qint64 nowMs) const {
  const auto it = entries_.constFind(id);
  return it == entries_.constEnd() ? 0 : decayed(it.value(), nowMs);
}

}  // namespace wabar::apps
