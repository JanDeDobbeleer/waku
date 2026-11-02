#include "features/color_scheme/image_stats_cache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <vector>

#include "kernel/qt/file_write.h"

namespace waku::color_scheme {

ImageStatsCache::ImageStatsCache(QString file) : file_(std::move(file)) { load(); }

QString ImageStatsCache::keyFor(const QString& path) {
  const QFileInfo info(path);
  if (!info.isFile()) return {};
  return QDir::cleanPath(info.absoluteFilePath()).toLower() + QLatin1Char('|') +
         QString::number(info.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|') +
         QString::number(info.size());
}

std::optional<quint32> ImageStatsCache::seed(const QString& key) const {
  QMutexLocker lock(&mutex_);
  const auto it = entries_.constFind(key);
  return it == entries_.constEnd() ? std::nullopt : it->seed;
}

std::optional<double> ImageStatsCache::luminance(const QString& key) const {
  QMutexLocker lock(&mutex_);
  const auto it = entries_.constFind(key);
  return it == entries_.constEnd() ? std::nullopt : it->luminance;
}

void ImageStatsCache::setSeed(const QString& key, quint32 argb) {
  if (key.isEmpty()) return;
  QMutexLocker lock(&mutex_);
  Entry& entry = entries_[key];
  entry.seed = argb;
  entry.touched = QDateTime::currentMSecsSinceEpoch();
  save();
}

void ImageStatsCache::setLuminance(const QString& key, double value) {
  if (key.isEmpty()) return;
  QMutexLocker lock(&mutex_);
  Entry& entry = entries_[key];
  entry.luminance = value;
  entry.touched = QDateTime::currentMSecsSinceEpoch();
  save();
}

void ImageStatsCache::load() {
  if (file_.isEmpty()) return;
  QFile in(file_);
  if (!in.open(QIODevice::ReadOnly)) return;
  QJsonParseError error;
  const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !doc.isObject()) return;
  const QJsonObject root = doc.object();
  for (auto it = root.begin(); it != root.end(); ++it) {
    if (!it.value().isObject()) continue;
    const QJsonObject value = it.value().toObject();
    Entry entry;
    if (value.value(QStringLiteral("seed")).isDouble()) {
      entry.seed = static_cast<quint32>(value.value(QStringLiteral("seed")).toDouble());
    }
    if (value.value(QStringLiteral("lum")).isDouble()) {
      entry.luminance = value.value(QStringLiteral("lum")).toDouble();
    }
    entry.touched = static_cast<qint64>(value.value(QStringLiteral("t")).toDouble());
    if (entry.seed || entry.luminance) entries_.insert(it.key(), entry);
  }
}

void ImageStatsCache::save() {
  if (file_.isEmpty()) return;
  if (entries_.size() > kMaxEntries) {
    std::vector<std::pair<qint64, QString>> order;
    for (auto it = entries_.constBegin(); it != entries_.constEnd(); ++it) {
      order.emplace_back(it->touched, it.key());
    }
    std::sort(order.begin(), order.end());
    const size_t drop = order.size() - kMaxEntries;
    for (size_t i = 0; i < drop; ++i) entries_.remove(order[i].second);
  }
  QJsonObject root;
  for (auto it = entries_.constBegin(); it != entries_.constEnd(); ++it) {
    QJsonObject value;
    if (it->seed) value.insert(QStringLiteral("seed"), static_cast<double>(*it->seed));
    if (it->luminance) value.insert(QStringLiteral("lum"), *it->luminance);
    value.insert(QStringLiteral("t"), static_cast<double>(it->touched));
    root.insert(it.key(), value);
  }
  kernel::writeFileAtomic(file_, QJsonDocument(root).toJson(QJsonDocument::Compact));
}

}  // namespace waku::color_scheme
