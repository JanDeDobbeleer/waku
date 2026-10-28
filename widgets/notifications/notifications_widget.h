#pragma once

#include <QPointer>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "platform/notifications.h"

namespace yasb::widgets {

// yasb.notifications.NotificationsWidget. The count service is shared (see
// platform/notifications.h); every slot here runs on the GUI thread.
//
// Not ported: the toggle_menu popup (grouped toast list, per-app and per-toast dismiss, toast
// activation, do-not-disturb button). Its `menu` options are accepted and ignored.
class NotificationsWidget final : public BaseWidget {
  Q_OBJECT

 public:
  static constexpr int kMaxCount = 9;

  NotificationsWidget(const WidgetContext& context, platform::NotificationService* service);

  int count() const { return count_; }
  // Test hook: the name of the system function toggle_notification runs.
  static QString notificationCenterFunction();

 private:
  bool event(QEvent* event) override;
  void onCountChanged(int count);
  void updateLabel();
  void toggleLabel();
  void toggleNotification();
  void clearNotifications();
  void applyVisibility();

  config::options::NotificationsConfig options_;
  QPointer<platform::NotificationService> service_;
  int count_ = 0;
  bool hiddenWhenEmpty_ = false;
  bool hiddenByUs_ = false;
};

// Registers yasb.notifications.NotificationsWidget; the shared NotificationService is created on
// first use.
void registerNotificationsWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets
