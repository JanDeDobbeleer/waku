#pragma once

#include <QDateTime>
#include <QHash>
#include <QLabel>
#include <QObject>
#include <QPixmap>
#include <QPointer>
#include <QScrollArea>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <optional>
#include <vector>

#include "platform/dnd.h"
#include "platform/notifications.h"
#include "widgets/menu_support/menu_popup.h"
#include "widgets/notifications/notification_widgets.h"

namespace yasb::bar {
class GlobalState;
}

namespace yasb::widgets {

constexpr int kCollapsedCount = 2;  // notifications shown per app before "N more"
constexpr int kAvatarSize = 48;     // logical px, the logo size the toast docs give
constexpr int kPictureMaxHeight = 240;
constexpr int kPictureRefreshDelayMs = 50;
inline const char* const kNotificationSettingsUri = "ms-settings:privacy-notifications";

struct NotificationsMenuOptions {
  bool blur = true;
  bool roundCorners = true;
  QString roundCornersType = QStringLiteral("normal");
  QString borderColor = QStringLiteral("System");
  QString alignment = QStringLiteral("right");
  QString direction = QStringLiteral("down");
  int offsetLeft = 0;
  int offsetTop = 6;
  int appIconSize = 16;
  QString iconDefault;
  QString iconDndOn;
  QString iconDndOff;
  QString iconDismiss;
  bool tooltip = true;
};

// Everything the menu does to the system, replaceable so tests never touch Focus Assist, Settings
// or the real icon cache.
struct NotificationsMenuHooks {
  std::function<QString()> dndStatus;
  std::function<void(const QString&)> setDndStatus;
  std::function<void(const QString&)> openUri;
  std::function<void()> toggleNotificationCenter;
};

// time_utils.get_relative_time(iso, short=True) for a timestamp and "now" (both with an offset).
// "now", "3m", "5h", "2d", then "5 Mar" within a year and "Mar 2024" after.
QString shortRelativeTime(const QDateTime& created, const QDateTime& now);

// MenuBuilder: the toggle_menu popup of the notifications widget. Lives on the GUI thread; every
// slot catches its own exceptions.
class NotificationsMenu : public QObject {
  Q_OBJECT
 public:
  NotificationsMenu(QWidget* anchor, platform::NotificationService* service,
                    platform::DndWatcher* dndWatcher, NotificationsMenuOptions options,
                    NotificationsMenuHooks hooks, bar::GlobalState& globalState);
  ~NotificationsMenu() override;

  // _toggle_menu: closes the open popup or opens a new one.
  void toggle();
  void close();
  bool isOpen() const;

  MenuPopup* popup() const { return popup_; }
  QScrollArea* scrollArea() const { return scroll_; }
  QLabel* dndButton() const { return dnd_; }
  // Test hook: the popup's pending picture refresh (PICTURE_REFRESH_DELAY).
  bool pictureRefreshPending() const { return refreshTimer_.isActive(); }
  // Test hook: runs the refresh now.
  void refreshItems();

 private:
  void show();
  QWidget* createHeader();
  QScrollArea* createScrollArea();
  QWidget* createFooter();
  QWidget* createAppSection(const std::vector<platform::NotificationItem>& items);
  QWidget* createAppHeader(const platform::NotificationItem& item);
  QWidget* createNotificationItem(const platform::NotificationItem& item);
  QWidget* createEmptyMessage();
  QLabel* createButton(const QString& text, const QString& cssClass, std::function<void()> action);
  void onItemsChanged();
  void toggleExpanded(const QString& aumid);
  void dismiss(std::uint32_t id);
  void clearApp(const QString& aumid);
  void clearAll();
  void activate(const platform::NotificationItem& item);
  void openNotificationCenter();
  void openNotificationSettings();
  void toggleDnd();
  void updateDndButton(const QString& status);
  void loadPictures(const std::vector<platform::NotificationItem>& items);
  void loadAppIcons(const std::vector<platform::NotificationItem>& items);
  void onImageLoaded(const QString& key, const QImage& image);
  QPixmap appIcon(const QString& aumid) const;
  QWidget* createAvatar(const platform::NotificationItem& item);
  QWidget* createPicture(const QString& path, const QString& cssClass,
                         const std::optional<QSizeF>& aspect = std::nullopt);
  double dpr() const;

  static QString pictureKey(const QString& path, const QString& shape);
  static QString iconKey(const QString& aumid);

  QWidget* anchor_;
  QPointer<platform::NotificationService> service_;
  QPointer<platform::DndWatcher> dndWatcher_;
  NotificationsMenuOptions options_;
  NotificationsMenuHooks hooks_;
  bar::GlobalState& globalState_;

  QPointer<MenuPopup> popup_;
  QPointer<QScrollArea> scroll_;
  QPointer<QLabel> dnd_;
  QSet<QString> expanded_;
  std::vector<std::pair<QString, QPointer<QLabel>>> iconLabels_;

  QHash<QString, QPixmap> pictures_;
  QSet<QString> picturesLoading_;
  QHash<QString, QPixmap> icons_;
  QSet<QString> iconsLoading_;
  ImageSignals imageSignals_;
  QThreadPool pool_;
  QTimer refreshTimer_;
};

}  // namespace yasb::widgets
