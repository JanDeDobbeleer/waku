#include "features/notifications/notifications.h"

#include <QDebug>
#include <QMetaObject>
#include <exception>
#include <utility>

namespace wabar::notifications {

// Re-posts every source event to the service's thread. Events for a destroyed service are dropped
// by Qt together with the service's posted events.
class NotificationService::Sink final : public NotificationSink {
 public:
  explicit Sink(NotificationService* service) : service_(service) {}

  void countChanged(std::uint32_t count) override {
    NotificationService* service = service_;
    QMetaObject::invokeMethod(
        service,
        [service, count] {
          try {
            service->applyCount(count);
          } catch (const std::exception& error) {
            qWarning().noquote() << "Notification update failed:" << error.what();
          }
        },
        Qt::QueuedConnection);
  }

  void itemsChanged(std::optional<std::vector<NotificationItem>> items) override {
    NotificationService* service = service_;
    QMetaObject::invokeMethod(
        service,
        [service, items = std::move(items)]() mutable {
          try {
            service->applyItems(std::move(items));
          } catch (const std::exception& error) {
            qWarning().noquote() << "Notification list update failed:" << error.what();
          }
        },
        Qt::QueuedConnection);
  }

 private:
  NotificationService* service_;
};

NotificationService::NotificationService(std::unique_ptr<NotificationSource> source,
                                         QObject* parent)
    : QObject(parent), source_(std::move(source)), sink_(std::make_unique<Sink>(this)) {}

NotificationService::~NotificationService() { stop(); }

void NotificationService::start() {
  if (started_ || !source_) return;
  started_ = true;
  source_->start(sink_.get());
}

void NotificationService::stop() {
  if (started_ && source_) source_->stop();
  started_ = false;
}

void NotificationService::clearAll() {
  if (source_ && started_) source_->clearAll();
}

void NotificationService::refresh() {
  if (source_ && started_) source_->refresh();
}

void NotificationService::remove(std::uint32_t id) {
  if (source_ && started_) source_->remove(id);
}

void NotificationService::clearApp(const QString& aumid) {
  if (source_ && started_) source_->clearApp(aumid);
}

void NotificationService::activate(const NotificationItem& item) {
  if (source_ && started_) source_->activate(item);
}

void NotificationService::applyItems(std::optional<std::vector<NotificationItem>> items) {
  if (items == items_) return;
  items_ = std::move(items);
  emit itemsChanged();
}

void NotificationService::applyCount(std::uint32_t count) {
  const int value = count > 100000 ? 100000 : static_cast<int>(count);
  if (value == count_) return;
  count_ = value;
  emit countChanged(count_);
}

}  // namespace wabar::notifications
