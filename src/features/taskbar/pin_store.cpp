#include "features/taskbar/pin_store.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>

#include "kernel/qt/app_data.h"

namespace wabar::taskbar {

PinStore::PinStore(QString filePath, QObject* parent)
    : QObject(parent), filePath_(std::move(filePath)) {}

QString PinStore::defaultFile() {
  return QDir(wabar::kernel::appDataDir()).filePath(QStringLiteral("taskbar_pinned.json"));
}

qint64 PinStore::fileStamp() const {
  const QFileInfo info(filePath_);
  return info.exists() ? info.lastModified().toMSecsSinceEpoch() : -1;
}

void PinStore::load() {
  const qint64 stamp = fileStamp();
  if (stamp == stamp_ && stamp_ != -1) return;
  apps_.clear();
  order_.clear();
  stamp_ = stamp;
  if (stamp < 0) return;

  QFile file(filePath_);
  if (!file.open(QIODevice::ReadOnly)) return;
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    qDebug().noquote() << "Taskbar: could not load pinned apps:" << error.errorString();
    return;
  }
  const QJsonObject root = document.object();
  const QJsonObject apps = root.value(QStringLiteral("pinned_apps")).toObject();
  for (auto it = apps.begin(); it != apps.end(); ++it) apps_[it.key()] = it.value().toObject();
  for (const QJsonValue& id : root.value(QStringLiteral("pinned_order")).toArray()) {
    if (id.isString()) order_ << id.toString();
  }
}

void PinStore::save() {
  QJsonObject apps;
  for (const auto& [id, metadata] : apps_) apps[id] = metadata;
  QJsonArray order;
  for (const QString& id : order_) order.append(id);
  const QJsonObject root{{QStringLiteral("pinned_apps"), apps},
                         {QStringLiteral("pinned_order"), order}};
  QDir().mkpath(QFileInfo(filePath_).absolutePath());
  QFile file(filePath_);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    qWarning().noquote() << "Taskbar: error saving pinned apps:" << file.errorString();
    return;
  }
  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.close();
  stamp_ = fileStamp();
}

QString PinStore::text(const QString& uniqueId, const char* key) const {
  const auto found = apps_.find(uniqueId);
  return found == apps_.end() ? QString() : found->second.value(QLatin1String(key)).toString();
}

void PinStore::pin(const QString& uniqueId, const QJsonObject& metadata, int position) {
  apps_[uniqueId] = metadata;
  if (order_.contains(uniqueId)) {
    // Already in the order: move it to the requested position.
    if (position >= 0) {
      order_.removeAll(uniqueId);
      order_.insert(std::clamp(position, 0, static_cast<int>(order_.size())), uniqueId);
    }
  } else if (position >= 0) {
    order_.insert(std::clamp(position, 0, static_cast<int>(order_.size())), uniqueId);
  } else {
    order_.append(uniqueId);
  }
  save();
  emit pinnedAppsChanged(QStringLiteral("pin"), uniqueId);
}

bool PinStore::unpin(const QString& uniqueId) {
  if (!contains(uniqueId)) return false;
  apps_.erase(uniqueId);
  order_.removeAll(uniqueId);
  save();
  emit pinnedAppsChanged(QStringLiteral("unpin"), uniqueId);
  return true;
}

void PinStore::updateOrder(const QStringList& newOrder) {
  if (newOrder == order_) return;
  order_ = newOrder;
  save();
  emit pinnedAppsChanged(QStringLiteral("reorder"), QString());
}

}  // namespace wabar::taskbar
