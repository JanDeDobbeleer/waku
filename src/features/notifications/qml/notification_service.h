#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QVariantList>
#include <memory>

#include "features/notifications/notifications.h"

namespace waku::notifications {

// The process-wide NotificationService, shared by `Notifications` and the Notification* feed types
// so the listener runs once. Without a live desktop it has no source. GUI thread only; gone when
// the last holder lets go.
std::shared_ptr<NotificationService> acquireNotificationService();

// The Action Center toasts as a QML singleton, shared by every Notifications widget. Wraps
// NotificationService, which owns the WNF subscription and the listener worker thread;
// this object lives on the GUI thread and the platform service re-posts every source event to it.
// The engine destroys it, which stops the source and joins the worker thread. Without a source
// (the offscreen platform, where the real listener must not run) `available` stays false.
//
// `count` is the number of toasts in the Action Center. `unread` is true while there is any: a
// toast leaves the Action Center once it is dismissed or activated, so what remains is unread.
// `items` is the toast list, newest first, a list of maps with id, app, title, body and time (a
// short clock or date text built when the list arrives). `refresh()` re-reads the list; the popup
// calls it when it opens.
class Notifications : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available NOTIFY changed FINAL)
  Q_PROPERTY(int count READ count NOTIFY changed FINAL)
  Q_PROPERTY(bool unread READ unread NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList items READ items NOTIFY changed FINAL)

 public:
  explicit Notifications(QObject* parent = nullptr);
  // Test seams: a fake source (null means no source), or a service shared with a test's feed.
  Notifications(std::unique_ptr<NotificationSource> source, QObject* parent);
  Notifications(std::shared_ptr<NotificationService> service, QObject* parent);
  ~Notifications() override;

  bool available() const { return service_->hasSource(); }
  int count() const { return service_->count(); }
  bool unread() const { return service_->count() > 0; }
  QVariantList items() const { return items_; }

  Q_INVOKABLE void clear() { service_->clearAll(); }
  Q_INVOKABLE void dismiss(int id);
  Q_INVOKABLE void activate(int id);
  Q_INVOKABLE void refresh() { service_->refresh(); }

 signals:
  void changed();

 private:
  void onServiceChanged();

  std::shared_ptr<NotificationService> service_;
  QVariantList items_;
};

}  // namespace waku::notifications
