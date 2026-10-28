#include "widgets/notifications/notifications_widget.h"

#include <QDebug>
#include <QEvent>

#include "core/widgets/backdrop.h"
#include "core/widgets/polling_worker_registry.h"
#include "platform/shell_open.h"
#include "widgets/system_widget_helpers.h"

namespace yasb::widgets {

NotificationsWidget::NotificationsWidget(const WidgetContext& context,
                                         platform::NotificationService* service,
                                         platform::DndWatcher* dndWatcher,
                                         NotificationsMenuHooks hooks)
    : BaseWidget(
          QString::fromStdString(context.name), context.eventBus,
          detail::className(std::get<config::options::NotificationsConfig>(context.typedOptions),
                            "notification-widget ")
                  .trimmed() +
              " ",
          0),
      options_(std::get<config::options::NotificationsConfig>(context.typedOptions)),
      service_(service) {
  buildWidgetLabel(QString::fromStdString(options_.label),
                   QString::fromStdString(options_.label_alt));
  setCallbackBinding({QString::fromStdString(options_.callbacks.on_left),
                      QString::fromStdString(options_.callbacks.on_middle),
                      QString::fromStdString(options_.callbacks.on_right),
                      QStringLiteral("do_nothing")});
  registerCallback(QStringLiteral("toggle_label"), [this](const QStringList&) { toggleLabel(); });
  registerCallback(QStringLiteral("toggle_menu"), [this](const QStringList&) {
    if (menu_) menu_->toggle();
  });
  registerCallback(QStringLiteral("toggle_notification"),
                   [this](const QStringList&) { toggleNotification(); });
  registerCallback(QStringLiteral("clear_notifications"),
                   [this](const QStringList&) { clearNotifications(); });

  const auto& menu = options_.menu;
  NotificationsMenuOptions menuOptions;
  menuOptions.blur = menu.blur;
  menuOptions.roundCorners = menu.round_corners;
  menuOptions.roundCornersType = QString::fromStdString(menu.round_corners_type);
  menuOptions.borderColor = QString::fromStdString(menu.border_color);
  menuOptions.alignment = QString::fromStdString(menu.alignment);
  menuOptions.direction = QString::fromStdString(menu.direction);
  menuOptions.offsetLeft = menu.offset_left;
  menuOptions.offsetTop = menu.offset_top;
  menuOptions.appIconSize = menu.app_icon_size;
  menuOptions.iconDefault = QString::fromStdString(options_.icons.default_);
  menuOptions.iconDndOn = QString::fromStdString(options_.icons.dnd_on);
  menuOptions.iconDndOff = QString::fromStdString(options_.icons.dnd_off);
  menuOptions.iconDismiss = QString::fromStdString(options_.icons.dismiss);
  menuOptions.tooltip = options_.tooltip;
  if (!hooks.dndStatus) hooks.dndStatus = [] { return platform::dndStatus(); };
  if (!hooks.setDndStatus)
    hooks.setDndStatus = [](const QString& mode) { platform::setDndStatus(mode); };
  if (!hooks.openUri) hooks.openUri = [](const QString& uri) { platform::shellOpen(uri); };
  if (!hooks.toggleNotificationCenter)
    hooks.toggleNotificationCenter = [this] { toggleNotification(); };
  menu_ = new NotificationsMenu(this, service_, dndWatcher, std::move(menuOptions),
                                std::move(hooks), context.globalState);

  if (service_) {
    count_ = service_->count();
    connect(service_, &platform::NotificationService::countChanged, this,
            [this](int count) { onCountChanged(count); });
  }
  hiddenWhenEmpty_ = count_ == 0 && options_.hide_empty;
  updateLabel();
  applyVisibility();
}

void NotificationsWidget::onCountChanged(int count) {
  try {
    count_ = count;
    hiddenWhenEmpty_ = count == 0 && options_.hide_empty;
    applyVisibility();
    updateLabel();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Notifications update failed:" << error.what();
  }
}

// Python: setVisible(True) when notifications arrive, setVisible(False) when empty and hide_empty.
// A widget without a parent is not shown, so it never opens as a stray top-level window.
void NotificationsWidget::applyVisibility() {
  if (hiddenWhenEmpty_) {
    hide();
    hiddenByUs_ = true;
  } else if (hiddenByUs_) {
    hiddenByUs_ = false;
    show();
  }
}

bool NotificationsWidget::event(QEvent* event) {
  const bool handled = BaseWidget::event(event);
  if (event->type() == QEvent::ParentChange && parentWidget() && !hiddenWhenEmpty_) show();
  return handled;
}

void NotificationsWidget::toggleLabel() {
  toggleAlt();
  updateLabel();
}

QString NotificationsWidget::notificationCenterFunction() {
  // Python is_windows_10(): major 10 and build < 22000 (build 0 means unreadable, treat as 11).
  const unsigned build = windowsBuild();
  return build != 0 && build < 22000 ? QStringLiteral("quick_settings")
                                     : QStringLiteral("notification_center");
}

void NotificationsWidget::toggleNotification() {
  const QString name = notificationCenterFunction();
  const auto& functions = systemFunctions();
  const auto found = functions.find(name);
  if (found == functions.end()) {
    qWarning().noquote() << "System function" << name << "is not registered";
    return;
  }
  found->second();
}

void NotificationsWidget::clearNotifications() {
  if (service_) service_->clearAll();
}

// Port of NotificationsWidget._update_label.
void NotificationsWidget::updateLabel() {
  if (hiddenWhenEmpty_) {
    hide();
    return;
  }
  const bool alt = showAlt();
  const auto& active = alt ? labelsAlt() : labels();
  const QString content = QString::fromStdString(alt ? options_.label_alt : options_.label);
  const QString icon =
      QString::fromStdString(count_ > 0 ? options_.icons.new_ : options_.icons.default_);
  const QString countText = count_ > kMaxCount ? QString::number(kMaxCount) + QStringLiteral("+")
                                               : QString::number(count_);
  const std::vector<std::pair<QString, QString>> replacements = {{"{count}", countText},
                                                                 {"{icon}", icon}};
  const auto parts = splitLabelParts(content, alt);
  size_t index = 0;
  for (const LabelPart& part : parts) {
    const QString text = replacePlaceholders(part.text, replacements).trimmed();
    if ((!part.isIcon && text.isEmpty()) || index >= active.size()) continue;
    QLabel* label = active[index];
    label->setText(text);
    QString css = label->property("class").toString();
    if (count_ > 0) {
      if (!css.contains(QStringLiteral("new-notification")))
        css += QStringLiteral(" new-notification");
    } else {
      css.replace(QStringLiteral(" new-notification"), QString());
    }
    BaseWidget::setLabelClass(label, css.trimmed());
    ++index;
  }
}

void registerNotificationsWidgets(WidgetRegistry& registry) {
  registry.add("yasb.notifications.NotificationsWidget", [](const WidgetContext& context) {
    auto* service = context.pollingWorkers.service<platform::NotificationService>(
        platform::NotificationService::kServiceKey, [] {
          auto* created =
              new platform::NotificationService(platform::createSystemNotificationSource());
          created->start();
          return created;
        });
    // DndService.initialize_wnf_listener: one subscription shared by every notifications widget.
    auto* watcher =
        context.pollingWorkers.service<platform::DndWatcher>(platform::DndWatcher::kServiceKey, [] {
          auto* created = new platform::DndWatcher;
          created->start();
          return created;
        });
    return new NotificationsWidget(context, service, watcher);
  });
}

}  // namespace yasb::widgets
