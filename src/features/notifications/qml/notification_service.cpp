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

Notifications::Notifications(QObject* parent) : Notifications(systemSource(), parent) {}

Notifications::Notifications(std::unique_ptr<NotificationSource> source, QObject* parent)
    : QObject(parent), hasSource_(source != nullptr), service_(std::move(source), this) {
  connect(&service_, &NotificationService::countChanged, this, &Notifications::onServiceChanged);
  connect(&service_, &NotificationService::itemsChanged, this, &Notifications::onServiceChanged);
  service_.start();
}

Notifications::~Notifications() { service_.stop(); }

void Notifications::onServiceChanged() {
  items_.clear();
  if (const auto& items = service_.items()) {
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

void Notifications::dismiss(int id) { service_.remove(static_cast<std::uint32_t>(id)); }

void Notifications::activate(int id) {
  const auto& items = service_.items();
  if (!items) return;
  for (const auto& item : *items) {
    if (static_cast<int>(item.id) == id) {
      service_.activate(item);
      return;
    }
  }
}

}  // namespace wabar::notifications
