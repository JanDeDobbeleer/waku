#pragma once

#include <QPointer>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "platform/dnd.h"
#include "platform/notifications.h"
#include "widgets/notifications/notifications_menu.h"

namespace yasb::widgets {

// yasb.notifications.NotificationsWidget. The count service is shared (see
// platform/notifications.h); every slot here runs on the GUI thread.
//
// toggle_menu opens NotificationsMenu: the toast list grouped per app with per-app and per-toast
// dismiss, toast activation and a do-not-disturb button.
class NotificationsWidget final : public BaseWidget {
  Q_OBJECT

 public:
  static constexpr int kMaxCount = 9;

  // `dndWatcher` (optional) reports external Focus Assist changes to the menu; empty hook members
  // fall back to the real system calls.
  NotificationsWidget(const WidgetContext& context, platform::NotificationService* service,
                      platform::DndWatcher* dndWatcher = nullptr,
                      NotificationsMenuHooks hooks = {});

  NotificationsMenu* menu() const { return menu_; }

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
  NotificationsMenu* menu_ = nullptr;
  int count_ = 0;
  bool hiddenWhenEmpty_ = false;
  bool hiddenByUs_ = false;
};

// Registers yasb.notifications.NotificationsWidget; the shared NotificationService is created on
// first use.
void registerNotificationsWidgets(WidgetRegistry& registry);

}  // namespace yasb::widgets
