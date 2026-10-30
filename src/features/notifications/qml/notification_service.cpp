#include "features/notifications/qml/notification_service.h"

#include <QDate>
#include <QDateTime>
#include <QVariantMap>

#include "kernel/qt/live_desktop.h"

namespace wabar::notifications {
namespace {

QString timeText(const QDateTime& created) {
  if (!created.isValid()) return {};
  const QDateTime local = created.toLocalTime();
  return local.date() == QDate::currentDate() ? local.toString(QStringLiteral("HH:mm"))
                                              : local.toString(QStringLiteral("d MMM"));
}

std::unique_ptr<NotificationSource> systemSource() {
  // The listener and the WNF subscription read the real desktop; a desktop that is not live never
  // runs them.
  if (!kernel::liveDesktop()) return nullptr;
  return createSystemNotificationSource();
}

}  // namespace

std::shared_ptr<NotificationService> acquireNotificationService() {
  static std::weak_ptr<NotificationService> shared;
  if (auto existing = shared.lock()) return existing;
  auto created = std::make_shared<NotificationService>(systemSource());
  shared = created;
  return created;
}

Notifications::Notifications(QObject* parent)
    : Notifications(acquireNotificationService(), parent) {}

Notifications::Notifications(std::unique_ptr<NotificationSource> source, QObject* parent)
    : Notifications(std::make_shared<NotificationService>(std::move(source)), parent) {}

Notifications::Notifications(std::shared_ptr<NotificationService> service, QObject* parent)
    : QObject(parent), service_(std::move(service)) {
  connect(service_.get(), &NotificationService::countChanged, this,
          &Notifications::onServiceChanged);
  connect(service_.get(), &NotificationService::itemsChanged, this,
          &Notifications::onServiceChanged);
  service_->start();
}

// The service stops its source when its last holder lets go.
Notifications::~Notifications() = default;

void Notifications::onServiceChanged() {
  items_.clear();
  if (const auto& items = service_->items()) {
    for (const auto& item : *items) {
      items_.append(QVariantMap{{QStringLiteral("id"), static_cast<int>(item.id)},
                                {QStringLiteral("app"), item.appName},
                                {QStringLiteral("title"), item.title},
                                {QStringLiteral("body"), item.body},
                                {QStringLiteral("time"), timeText(item.createdAt)}});
    }
  }
  emit changed();
}

void Notifications::dismiss(int id) { service_->remove(static_cast<std::uint32_t>(id)); }

void Notifications::activate(int id) {
  const auto& items = service_->items();
  if (!items) return;
  for (const auto& item : *items) {
    if (static_cast<int>(item.id) == id) {
      service_->activate(item);
      return;
    }
  }
}

}  // namespace wabar::notifications
