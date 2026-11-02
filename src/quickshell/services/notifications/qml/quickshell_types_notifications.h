#pragma once

// The `Quickshell.Services.Notifications` module's types: the notifications feature's, registered
// again under Quickshell's names so that `import Quickshell.Services.Notifications` shows these
// names and no others of waku.services. No code: each struct only carries the registration.

#include <QtQml/qqmlregistration.h>

#include <QObject>

#include "features/notifications/qml/notification_feed.h"

namespace waku::quickshell {

struct QsNotificationUrgency {
  Q_GADGET
  QML_FOREIGN(waku::notifications::NotificationUrgency)
  QML_NAMED_ELEMENT(NotificationUrgency)
  QML_UNCREATABLE("NotificationUrgency only holds the enum")
};

struct QsNotificationCloseReason {
  Q_GADGET
  QML_FOREIGN(waku::notifications::NotificationCloseReason)
  QML_NAMED_ELEMENT(NotificationCloseReason)
  QML_UNCREATABLE("NotificationCloseReason only holds the enum")
};

struct QsNotification {
  Q_GADGET
  QML_FOREIGN(waku::notifications::NotificationToast)
  QML_NAMED_ELEMENT(Notification)
  QML_UNCREATABLE("Notification objects come from NotificationServer")
};

struct QsNotificationAction {
  Q_GADGET
  QML_FOREIGN(waku::notifications::NotificationAction)
  QML_NAMED_ELEMENT(NotificationAction)
  QML_UNCREATABLE("NotificationAction objects come from Notification")
};

struct QsNotificationServer {
  Q_GADGET
  QML_FOREIGN(waku::notifications::NotificationServer)
  QML_NAMED_ELEMENT(NotificationServer)
};

}  // namespace waku::quickshell
