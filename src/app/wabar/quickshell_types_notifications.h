#pragma once

// The `Quickshell.Services.Notifications` module's types: the notifications feature's, registered
// again under Quickshell's names so that `import Quickshell.Services.Notifications` shows these
// names and no others of wabar.services. No code: each struct only carries the registration.

#include <QtQml/qqmlregistration.h>

#include <QObject>

#include "features/notifications/qml/notification_feed.h"

namespace wabar::quickshell {

struct QsNotificationUrgency {
  Q_GADGET
  QML_FOREIGN(wabar::notifications::NotificationUrgency)
  QML_NAMED_ELEMENT(NotificationUrgency)
  QML_UNCREATABLE("NotificationUrgency only holds the enum")
};

struct QsNotificationCloseReason {
  Q_GADGET
  QML_FOREIGN(wabar::notifications::NotificationCloseReason)
  QML_NAMED_ELEMENT(NotificationCloseReason)
  QML_UNCREATABLE("NotificationCloseReason only holds the enum")
};

struct QsNotification {
  Q_GADGET
  QML_FOREIGN(wabar::notifications::NotificationToast)
  QML_NAMED_ELEMENT(Notification)
  QML_UNCREATABLE("Notification objects come from NotificationServer")
};

struct QsNotificationServer {
  Q_GADGET
  QML_FOREIGN(wabar::notifications::NotificationServer)
  QML_NAMED_ELEMENT(NotificationServer)
};

}  // namespace wabar::quickshell
