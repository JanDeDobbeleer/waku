#include "features/notifications/qml/notification_feed.h"

#include <QDate>
#include <algorithm>
#include <utility>
#include <vector>

#include "features/notifications/qml/notification_service.h"

namespace wabar::notifications {

NotificationUrgency::Enum urgencyForScenario(const QString& scenario) {
  if (scenario.compare(QStringLiteral("alarm"), Qt::CaseInsensitive) == 0 ||
      scenario.compare(QStringLiteral("incomingCall"), Qt::CaseInsensitive) == 0 ||
      scenario.compare(QStringLiteral("urgent"), Qt::CaseInsensitive) == 0) {
    return NotificationUrgency::Critical;
  }
  return NotificationUrgency::Normal;
}

NotificationToast::NotificationToast(std::shared_ptr<NotificationService> service, QObject* parent)
    : QObject(parent), service_(std::move(service)) {}

QString NotificationToast::image() const {
  return item_.image.isEmpty() ? item_.hero : item_.image;
}

QString NotificationToast::timeText() const {
  if (!item_.createdAt.isValid()) return {};
  const QDateTime local = item_.createdAt.toLocalTime();
  return local.date() == QDate::currentDate() ? local.toString(QStringLiteral("HH:mm"))
                                              : local.toString(QStringLiteral("d MMM"));
}

void NotificationToast::update(const NotificationItem& item) {
  const bool appName = item.appName != item_.appName;
  const bool appIcon = item.logo != item_.logo;
  const bool summary = item.title != item_.title;
  const bool body = item.body != item_.body;
  const QString oldImage = image();
  const auto oldUrgency = urgency();
  const bool time = item.createdAt != item_.createdAt;
  item_ = item;
  if (appName) emit appNameChanged();
  if (appIcon) emit appIconChanged();
  if (summary) emit summaryChanged();
  if (body) emit bodyChanged();
  if (image() != oldImage) emit imageChanged();
  if (urgency() != oldUrgency) emit urgencyChanged();
  if (time) emit timeChanged();
}

void NotificationToast::setTracked(bool tracked) {
  if (tracked == tracked_) return;
  tracked_ = tracked;
  emit trackedChanged();
}

void NotificationToast::dismiss() { service_->remove(item_.id); }

void NotificationToast::activate() { service_->activate(item_); }

int NotificationModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(toasts_.size());
}

QVariant NotificationModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= toasts_.size() ||
      role != Qt::UserRole) {
    return {};
  }
  return QVariant::fromValue(toasts_.at(index.row()));
}

QHash<int, QByteArray> NotificationModel::roleNames() const {
  return {{Qt::UserRole, QByteArrayLiteral("modelData")}};
}

NotificationToast* NotificationModel::find(int id) const {
  for (NotificationToast* toast : toasts_) {
    if (toast->id() == id) return toast;
  }
  return nullptr;
}

void NotificationModel::insert(int row, NotificationToast* toast) {
  beginInsertRows({}, row, row);
  toasts_.insert(row, toast);
  endInsertRows();
  emit valuesChanged();
}

void NotificationModel::remove(NotificationToast* toast) {
  const int row = static_cast<int>(toasts_.indexOf(toast));
  if (row < 0) return;
  beginRemoveRows({}, row, row);
  toasts_.removeAt(row);
  endRemoveRows();
  emit valuesChanged();
}

void NotificationModel::move(NotificationToast* toast, int row) {
  const int from = static_cast<int>(toasts_.indexOf(toast));
  if (from < 0 || from == row) return;
  if (!beginMoveRows({}, from, from, {}, row > from ? row + 1 : row)) return;
  toasts_.move(from, row);
  endMoveRows();
  emit valuesChanged();
}

NotificationHub::NotificationHub(std::shared_ptr<NotificationService> service, QObject* parent)
    : QObject(parent),
      service_(std::move(service)),
      model_(this),
      started_(QDateTime::currentDateTimeUtc()) {
  connect(service_.get(), &NotificationService::itemsChanged, this, &NotificationHub::sync);
  service_->start();
  sync();
}

// The toasts are children of this object; the shared service stops its source when its last
// holder lets go.
NotificationHub::~NotificationHub() = default;

void NotificationHub::sync() {
  const auto& items = service_->items();
  const std::vector<NotificationItem> empty;
  const std::vector<NotificationItem>& list = items ? *items : empty;

  // Gone from the Action Center.
  for (NotificationToast* toast : model_.values()) {
    const bool present = std::any_of(list.begin(), list.end(), [&](const NotificationItem& item) {
      return static_cast<int>(item.id) == toast->id();
    });
    if (present) continue;
    model_.remove(toast);
    emit toast->closed(NotificationCloseReason::Dismissed);
    toast->deleteLater();
  }

  // Newest first, in the order the service reports.
  std::vector<NotificationToast*> added;
  int row = 0;
  for (const NotificationItem& item : list) {
    NotificationToast* toast = model_.find(static_cast<int>(item.id));
    if (!toast) {
      toast = new NotificationToast(service_, this);
      toast->update(item);
      model_.insert(row, toast);
      // A toast older than the hub was in the Action Center before the bar started.
      if (!item.createdAt.isValid() || item.createdAt.toUTC() >= started_.addSecs(-2)) {
        added.push_back(toast);
      }
    } else {
      toast->update(item);
      model_.move(toast, row);
    }
    ++row;
  }
  for (NotificationToast* toast : added) emit toastAdded(toast);
}

std::shared_ptr<NotificationHub> acquireNotificationHub() {
  static std::weak_ptr<NotificationHub> shared;
  if (auto existing = shared.lock()) return existing;
  auto created = std::make_shared<NotificationHub>(acquireNotificationService());
  shared = created;
  return created;
}

NotificationFeed::NotificationFeed(QObject* parent)
    : NotificationFeed(acquireNotificationHub(), parent) {}

NotificationFeed::NotificationFeed(std::shared_ptr<NotificationHub> hub, QObject* parent)
    : QObject(parent), hub_(std::move(hub)) {
  connect(hub_.get(), &NotificationHub::toastAdded, this, &NotificationFeed::toastAdded);
}

NotificationServer::NotificationServer(QObject* parent)
    : NotificationServer(acquireNotificationHub(), parent) {}

NotificationServer::NotificationServer(std::shared_ptr<NotificationHub> hub, QObject* parent)
    : QObject(parent), hub_(std::move(hub)), tracked_(this) {
  connect(hub_.get(), &NotificationHub::toastAdded, this, &NotificationServer::onToastAdded);
}

void NotificationServer::onToastAdded(NotificationToast* toast) {
  connect(toast, &NotificationToast::trackedChanged, this, [this, toast] {
    if (toast->tracked()) {
      if (!tracked_.values().contains(toast)) tracked_.insert(tracked_.count(), toast);
    } else {
      tracked_.remove(toast);
    }
  });
  connect(toast, &QObject::destroyed, this,
          [this, toast] { tracked_.remove(static_cast<NotificationToast*>(toast)); });
  emit notification(toast);
}

}  // namespace wabar::notifications
