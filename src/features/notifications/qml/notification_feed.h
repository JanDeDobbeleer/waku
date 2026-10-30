#pragma once

#include <QtQml/qqmlregistration.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

#include "features/notifications/notifications.h"

namespace wabar::notifications {

// Enum holders for QML, as in Quickshell.Services.Notifications.
class NotificationUrgency : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("NotificationUrgency only holds the enum")

 public:
  enum Enum { Low = 0, Normal = 1, Critical = 2 };
  Q_ENUM(Enum)
};

class NotificationCloseReason : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("NotificationCloseReason only holds the enum")

 public:
  enum Enum { Expired = 1, Dismissed = 2, CloseRequested = 3 };
  Q_ENUM(Enum)
};

// Toast `scenario` to urgency: alarm, incomingCall and urgent are Critical, the rest Normal.
// Windows toasts have no low priority.
NotificationUrgency::Enum urgencyForScenario(const QString& scenario);

// One toast of the Action Center in the shape of Quickshell's Notification. Windows owns the
// toast, so these members never carry data: `actions` is always empty, `resident`,
// `hasActionIcons`, `hasInlineReply` and `transient` are false, `expireTimeout` is 0 (the OS
// decides) and `hints` is empty. `dismiss()` and `close()` remove the toast from the Action
// Center; `expire()` does nothing. GUI thread only.
class NotificationToast : public QObject {
  Q_OBJECT
  QML_NAMED_ELEMENT(Notification)
  QML_UNCREATABLE("Notification objects come from NotificationServer and NotificationFeed")
  Q_PROPERTY(int id READ id CONSTANT FINAL)
  Q_PROPERTY(QString appName READ appName NOTIFY appNameChanged FINAL)
  Q_PROPERTY(QString appIcon READ appIcon NOTIFY appIconChanged FINAL)
  Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged FINAL)
  Q_PROPERTY(QString body READ body NOTIFY bodyChanged FINAL)
  Q_PROPERTY(QString image READ image NOTIFY imageChanged FINAL)
  Q_PROPERTY(wabar::notifications::NotificationUrgency::Enum urgency READ urgency NOTIFY
                 urgencyChanged FINAL)
  Q_PROPERTY(QDateTime time READ time NOTIFY timeChanged FINAL)
  Q_PROPERTY(QString timeText READ timeText NOTIFY timeChanged FINAL)
  Q_PROPERTY(double expireTimeout READ expireTimeout CONSTANT FINAL)
  Q_PROPERTY(bool resident READ resident CONSTANT FINAL)
  Q_PROPERTY(bool transient READ isTransient CONSTANT FINAL)
  Q_PROPERTY(bool hasActionIcons READ hasActionIcons CONSTANT FINAL)
  Q_PROPERTY(bool hasInlineReply READ hasInlineReply CONSTANT FINAL)
  Q_PROPERTY(QVariantList actions READ actions CONSTANT FINAL)
  Q_PROPERTY(QVariantMap hints READ hints CONSTANT FINAL)
  Q_PROPERTY(bool tracked READ tracked WRITE setTracked NOTIFY trackedChanged FINAL)
  Q_PROPERTY(QString aumid READ aumid CONSTANT FINAL)

 public:
  explicit NotificationToast(std::shared_ptr<NotificationService> service, QObject* parent);

  // Copies the item in; emits one signal per changed property.
  void update(const NotificationItem& item);

  int id() const { return static_cast<int>(item_.id); }
  QString appName() const { return item_.appName; }
  QString appIcon() const { return item_.logo; }
  QString summary() const { return item_.title; }
  QString body() const { return item_.body; }
  QString image() const;
  NotificationUrgency::Enum urgency() const { return urgencyForScenario(item_.scenario); }
  QDateTime time() const { return item_.createdAt; }
  QString timeText() const;
  double expireTimeout() const { return 0; }
  bool resident() const { return false; }
  bool isTransient() const { return false; }
  bool hasActionIcons() const { return false; }
  bool hasInlineReply() const { return false; }
  QVariantList actions() const { return {}; }
  QVariantMap hints() const { return {}; }
  bool tracked() const { return tracked_; }
  void setTracked(bool tracked);
  QString aumid() const { return item_.aumid; }
  const NotificationItem& item() const { return item_; }

  Q_INVOKABLE void dismiss();
  Q_INVOKABLE void close() { dismiss(); }
  Q_INVOKABLE void expire() {}
  // Hands the toast to its app (what clicking it in the Action Center does).
  Q_INVOKABLE void activate();

 signals:
  void appNameChanged();
  void appIconChanged();
  void summaryChanged();
  void bodyChanged();
  void imageChanged();
  void urgencyChanged();
  void timeChanged();
  void trackedChanged();
  // The toast left the Action Center.
  void closed(wabar::notifications::NotificationCloseReason::Enum reason);

 private:
  friend class NotificationHub;
  std::shared_ptr<NotificationService> service_;
  NotificationItem item_;
  bool tracked_ = false;
};

// The toasts as a model (role `modelData`) with a `values` list, like Quickshell's ObjectModel.
class NotificationModel : public QAbstractListModel {
  Q_OBJECT
  QML_ANONYMOUS
  Q_PROPERTY(
      QList<wabar::notifications::NotificationToast*> values READ values NOTIFY valuesChanged FINAL)
  Q_PROPERTY(int count READ count NOTIFY valuesChanged FINAL)

 public:
  explicit NotificationModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  QList<NotificationToast*> values() const { return toasts_; }
  int count() const { return static_cast<int>(toasts_.size()); }
  NotificationToast* find(int id) const;
  void insert(int row, NotificationToast* toast);
  void remove(NotificationToast* toast);
  // Reports a changed order: moves `toast` to `row`.
  void move(NotificationToast* toast, int row);

 signals:
  void valuesChanged();

 private:
  QList<NotificationToast*> toasts_;
};

// The model behind the feed types: follows the shared NotificationService, keeps one
// NotificationToast per Action Center toast, newest first, and emits `toastAdded` for toasts that
// arrive while the bar runs. Toasts older than the hub (those already in the Action Center at
// start) fill the model silently. Not a QML type; `NotificationFeed` and `NotificationServer`
// share one through acquireNotificationHub().
class NotificationHub : public QObject {
  Q_OBJECT

 public:
  explicit NotificationHub(std::shared_ptr<NotificationService> service, QObject* parent = nullptr);
  ~NotificationHub() override;

  NotificationModel* model() { return &model_; }
  bool available() const { return service_->hasSource(); }
  void clear() { service_->clearAll(); }
  void refresh() { service_->refresh(); }

 signals:
  void toastAdded(wabar::notifications::NotificationToast* toast);

 private:
  void sync();

  std::shared_ptr<NotificationService> service_;
  NotificationModel model_;
  QDateTime started_;
};

// The process-wide hub (system service). GUI thread only; gone when the last holder lets go.
std::shared_ptr<NotificationHub> acquireNotificationHub();

// A model of the recent Action Center toasts, for QML written against
// Quickshell.Services.Notifications. `notifications` lists every toast (newest first);
// `toastAdded(toast)` fires for each toast that arrives while the bar runs; `clear()` empties the
// Action Center.
class NotificationFeed : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(
      wabar::notifications::NotificationModel* notifications READ notifications CONSTANT FINAL)

 public:
  explicit NotificationFeed(QObject* parent = nullptr);
  // Test seam: a hub over a fake source.
  NotificationFeed(std::shared_ptr<NotificationHub> hub, QObject* parent);

  bool available() const { return hub_->available(); }
  NotificationModel* notifications() { return hub_->model(); }

  Q_INVOKABLE void clear() { hub_->clear(); }
  Q_INVOKABLE void refresh() { hub_->refresh(); }

 signals:
  void toastAdded(wabar::notifications::NotificationToast* toast);

 private:
  std::shared_ptr<NotificationHub> hub_;
};

// Quickshell's NotificationServer: creating one in QML makes `notification(notif)` fire for each
// new toast. Windows owns the toast popups and there is no server to register, so the capability
// flags are accepted and ignored. `trackedNotifications` lists the toasts QML set `tracked` on.
class NotificationServer : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool keepOnReload MEMBER keepOnReload_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool actionsSupported MEMBER actionsSupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool actionIconsSupported MEMBER actionIconsSupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool bodySupported MEMBER bodySupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool bodyMarkupSupported MEMBER bodyMarkupSupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(
      bool bodyHyperlinksSupported MEMBER bodyHyperlinksSupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool bodyImagesSupported MEMBER bodyImagesSupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool imageSupported MEMBER imageSupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool inlineReplySupported MEMBER inlineReplySupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(bool persistenceSupported MEMBER persistenceSupported_ NOTIFY capabilitiesChanged)
  Q_PROPERTY(wabar::notifications::NotificationModel* trackedNotifications READ trackedNotifications
                 CONSTANT FINAL)

 public:
  explicit NotificationServer(QObject* parent = nullptr);
  // Test seam: a hub over a fake source.
  NotificationServer(std::shared_ptr<NotificationHub> hub, QObject* parent);

  NotificationModel* trackedNotifications() { return &tracked_; }

 signals:
  void notification(wabar::notifications::NotificationToast* notification);
  void capabilitiesChanged();

 private:
  void onToastAdded(NotificationToast* toast);

  std::shared_ptr<NotificationHub> hub_;
  NotificationModel tracked_;
  bool keepOnReload_ = true;
  bool actionsSupported_ = false;
  bool actionIconsSupported_ = false;
  bool bodySupported_ = true;
  bool bodyMarkupSupported_ = false;
  bool bodyHyperlinksSupported_ = false;
  bool bodyImagesSupported_ = false;
  bool imageSupported_ = false;
  bool inlineReplySupported_ = false;
  bool persistenceSupported_ = false;
};

}  // namespace wabar::notifications
