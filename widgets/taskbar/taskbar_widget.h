#pragma once

#include <QBoxLayout>
#include <QFrame>
#include <QJsonObject>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "platform/dwm_thumbnail.h"
#include "platform/pin_support.h"
#include "platform/window_system.h"
#include "platform/window_tracker.h"

namespace yasb::bar {
class GlobalState;
}

class QMenu;

namespace yasb::widgets {

class TaskbarWidget;
class TaskbarIcons;
class TaskbarDropHandler;
class PreviewManager;
class PinStore;
class RecycleBinMonitor;

// A pinned app that is not running has no window: its button stands for a negative "pseudo" handle
// (Python: -(abs(hash(unique_id)) % 10**9 + 10**9)).
inline bool isPseudoHwnd(platform::WindowHandle hwnd) {
  return static_cast<std::intptr_t>(hwnd) < 0;
}
platform::WindowHandle pseudoHwndFor(const QString& uniqueId);

// Port of DraggableAppButton: one taskbar button, shared by every window of an app when grouping
// is on. Dragging it past the drag threshold reorders it (see TaskbarDropHandler).
class AppButton final : public QFrame {
  Q_OBJECT

 public:
  AppButton(TaskbarWidget* taskbar, platform::WindowHandle hwnd);

  // The window the button currently stands for (the focused one of a group).
  platform::WindowHandle hwnd() const { return hwnd_; }
  void setHwnd(platform::WindowHandle hwnd);

  QLabel* iconLabel = nullptr;
  QFrame* titleWrapper = nullptr;
  QLabel* titleLabel = nullptr;
  QLabel* countLabel = nullptr;
  QPointer<QPropertyAnimation> titleAnimation;
  bool blinked = false;

 protected:
  void enterEvent(QEnterEvent* event) override;
  void leaveEvent(QEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void checkHidePreview();
  void onPreviewTimeout();
  bool maybeStartDrag(QMouseEvent* event);
  void restartPreviewIfHovered();

  TaskbarWidget* taskbar_;
  platform::WindowHandle hwnd_;
  QTimer previewTimer_;
  bool dragging_ = false;
  bool leftPressed_ = false;
  QPoint pressGlobalPos_;
};

// A blink timer driving one button's class property.
class FlashTimer final : public QTimer {
  Q_OBJECT

 public:
  FlashTimer(QObject* parent, AppButton* container) : QTimer(parent), container(container) {}
  int count = 0;
  QPointer<AppButton> container;
};

// Everything the widget needs from the OS, so tests can substitute fakes.
struct TaskbarDeps {
  std::shared_ptr<platform::WindowSystem> system;
  // The shared tracker; null when a test drives the on* slots directly.
  platform::WindowTracker* tracker = nullptr;
  // Owned by the widget; null builds the asynchronous one.
  TaskbarIcons* icons = nullptr;
  std::shared_ptr<platform::ThumbnailBackend> thumbnails;
  std::shared_ptr<platform::LivePreviewBackend> livePreview;
  // Pinned apps: shell operations (null builds the Win32 ones) and the shared store of
  // taskbar_pinned.json. With no store the widget makes its own at `pinFile`, or at
  // %LOCALAPPDATA%\YASB	askbar_pinned.json when that is empty; tests always pass a temp
  // file.
  std::shared_ptr<platform::PinSupport> pins;
  PinStore* pinStore = nullptr;
  QString pinFile;
  // The Recycle Bin query runs on a worker; tests turn that off to stay deterministic.
  bool asyncRecycleBin = true;
};

// yasb.taskbar.TaskbarWidget: one button per window (or per app with grouping), a count badge,
// animated add and remove, attention flashing, the hover preview with peek, pinned apps with drag
// reorder, and the full context menu.
class TaskbarWidget final : public BaseWidget {
  Q_OBJECT

 public:
  explicit TaskbarWidget(const WidgetContext& context, TaskbarDeps deps);
  ~TaskbarWidget() override;

  // --- signals from the tracker (slots are public so tests can feed them directly)
  void onWindowAdded(platform::WindowHandle hwnd, const platform::WindowInfo& info);
  void onWindowRemoved(platform::WindowHandle hwnd, const platform::WindowInfo& info);
  void onWindowUpdated(platform::WindowHandle hwnd, const platform::WindowInfo& info);
  void onWindowMinimizeChanged(platform::WindowHandle hwnd, bool minimized);
  void onWindowMonitorChanged(platform::WindowHandle hwnd, const platform::WindowInfo& info);

  // --- interaction
  void onButtonClicked(platform::WindowHandle hwnd, QWidget* button);
  void bringToForeground(platform::WindowHandle hwnd);
  void showPreviewForHwnd(platform::WindowHandle hwnd, QWidget* anchor);
  void hidePreview();
  QMenu* showContextMenu(platform::WindowHandle hwnd, const QPoint& pos);
  // Pinned-app drag in progress: updates are suspended and the preview is hidden.
  void setDragging(bool active);
  // Raises a window while a file is dragged over its button.
  void ensureForeground(platform::WindowHandle hwnd);

  // --- pinned apps (PinManager)
  void pinApp(platform::WindowHandle hwnd);
  void unpinApp(platform::WindowHandle hwnd);
  bool isAppPinned(platform::WindowHandle hwnd) const { return runningPinned_.count(hwnd) != 0; }
  // Unpins an app that is not running (its button has a pseudo handle).
  void unpinPinnedOnlyApp(const QString& uniqueId, platform::WindowHandle pseudoHwnd);
  // Launches a pinned app by id, or by the pseudo/real handle of its button.
  void launchPinnedApp(const QString& uniqueId, const QString& extraArguments = QString());
  void launchPinnedApp(platform::WindowHandle hwnd);
  // Loads the pinned file and shows a button for every pinned app (done once, on first show).
  void displayPinnedApps();
  void onPinnedAppsChanged(const QString& action, const QString& uniqueId);
  void updatePinnedOrderFromLayout();
  PinStore* pinStore() const { return pinStore_; }
  RecycleBinMonitor* recycleBinMonitor() const { return recycleBin_; }
  TaskbarDropHandler* dropHandler() const { return dropHandler_; }
  // Button of a pinned app that is not running; null when it runs or is not pinned.
  AppButton* pinnedOnlyButton(const QString& uniqueId) const;
  // Unique ids of the buttons in layout order ("" for an unpinned running window).
  QStringList layoutUniqueIds() const;
  bool previewEnabled() const { return previewEnabled_; }
  int previewDelay() const { return previewDelay_; }
  PreviewManager* previewManager() const { return previewManager_; }

  // CSS class of the button a window belongs to (get_container_class).
  QString containerClass(platform::WindowHandle hwnd) const;
  // Title and icon recorded for a window; false when the taskbar does not show it.
  bool windowEntry(platform::WindowHandle hwnd, QString& title, QPixmap& icon) const;
  bool isWindowFlashing(platform::WindowHandle hwnd) const;

  // --- test and parity hooks
  AppButton* buttonFor(platform::WindowHandle hwnd) const;
  bool hasWindow(platform::WindowHandle hwnd) const { return windowButtons_.count(hwnd) != 0; }
  std::vector<AppButton*> buttons() const;
  std::vector<platform::WindowHandle> groupMembers(const QString& key) const;
  bool animating(platform::WindowHandle hwnd) const;
  void setInitialPopulation(bool value) { initialPopulation_ = value; }
  void setDevicePixelRatio(qreal value) { dpi_ = value; }
  bool contextMenuOpen() const { return contextMenuOpen_; }
  int flashTimerCount() const { return static_cast<int>(flashTimers_.size()); }
  void reloadIcons();

 protected:
  void showEvent(QShowEvent* event) override;
  bool event(QEvent* event) override;

 private:
  struct WindowEntry {
    QString title;
    QPixmap icon;
    platform::WindowHandle hwnd = 0;
    QString process;
    platform::WindowInfo info;
  };

  void connectTracker();
  bool shouldShowWindow(platform::WindowHandle hwnd, const platform::WindowInfo& info) const;
  void addWindowUi(platform::WindowHandle hwnd, const platform::WindowInfo& info);
  void removeWindowUi(platform::WindowHandle hwnd, const std::optional<platform::WindowInfo>& info,
                      bool immediate = false);
  void updateWindowUi(platform::WindowHandle hwnd, const platform::WindowInfo& info);
  void syncGroupButton(const QString& groupKey);
  AppButton* createAppContainer(const QString& title, const QPixmap& icon,
                                platform::WindowHandle hwnd);
  QPixmap appIcon(platform::WindowHandle hwnd);

  std::vector<platform::WindowHandle> membersOf(platform::WindowHandle hwnd) const;
  bool isMinimized(const std::vector<platform::WindowHandle>& members) const;
  void clearOthersSetForeground(std::optional<platform::WindowHandle> target);
  QString formatTitle(const QString& title) const;
  bool titleVisibility(platform::WindowHandle hwnd) const;
  void refreshTitleVisibility(platform::WindowHandle hwnd);
  void animateOrSetTitleVisible(AppButton* button, bool visible);

  void updateFlashState(platform::WindowHandle hwnd, AppButton* button);
  bool flashOwns(const QWidget* widget) const;
  void startFlashTimer(platform::WindowHandle hwnd, AppButton* button);
  void stopFlashTimer(platform::WindowHandle hwnd);

  void animateContainer(AppButton* container, int startWidth, int endWidth, int duration,
                        std::optional<platform::WindowHandle> hwnd = std::nullopt,
                        bool isRemoving = false);
  void insertButton(int position, AppButton* button);
  QWidget* layoutWidgetAt(int index) const;
  int hwndPosition(platform::WindowHandle hwnd) const;
  int insertPositionAfterPinned(platform::WindowHandle skip = 0) const;
  void updatePinnedStatus(platform::WindowHandle hwnd, bool pinned);
  AppButton* createPinnedContainer(const QString& title, const QPixmap& icon,
                                   platform::WindowHandle pseudo, const QString& uniqueId);
  // Builds, registers and inserts a pinned-only button (position -1 appends).
  void createPinnedButton(const QString& uniqueId, int position = -1);
  void recreatePinnedButton(const QString& uniqueId, int position);
  QPixmap pinnedIcon(const QString& uniqueId);
  QPixmap recycleBinIcon(bool empty);
  void startRecycleBinMonitor();
  void stopRecycleBinMonitor();
  void onRecycleBinUpdate(qint64 numItems);
  QString pinnedTitle(const QString& uniqueId) const;
  void removeButton(AppButton* button);
  int buttonCount() const;
  void showTaskbarWidget();
  void hideTaskbarWidget();
  void stopEvents();

  platform::WindowHandle hwndUnderCursor() const;
  void onToggleWindow();
  void onCloseApp();
  void onContextMenu();

  static void refreshStyle(std::initializer_list<QWidget*> widgets);
  void setClassProperty(QWidget* widget, const QString& cssClass);

  config::options::TaskbarConfig options_;
  bar::GlobalState& globalState_;
  std::shared_ptr<platform::WindowSystem> system_;
  QPointer<platform::WindowTracker> tracker_;
  TaskbarIcons* icons_ = nullptr;
  PreviewManager* previewManager_ = nullptr;

  bool animationEnabled_;
  int animationDuration_;
  bool groupingEnabled_;
  bool previewEnabled_;
  int previewDelay_;
  bool tooltip_;
  std::optional<qreal> dpi_;

  std::map<platform::WindowHandle, QPointer<AppButton>> hwndToWidget_;
  std::map<platform::WindowHandle, WindowEntry> windowButtons_;
  std::map<QString, std::vector<platform::WindowHandle>> groupHwnds_;
  std::map<platform::WindowHandle, QString> hwndToGroup_;
  std::map<platform::WindowHandle, QPointer<QPropertyAnimation>> animatingWidgets_;
  std::map<platform::WindowHandle, FlashTimer*> flashTimers_;
  std::set<platform::WindowHandle> minimizedHwnds_;
  bool contextMenuOpen_ = false;
  bool initialPopulation_ = false;
  bool trackerConnected_ = false;

  std::shared_ptr<platform::PinSupport> pins_;
  PinStore* pinStore_ = nullptr;
  TaskbarDropHandler* dropHandler_ = nullptr;
  RecycleBinMonitor* recycleBin_ = nullptr;
  bool recycleBinEmpty_ = true;
  std::map<std::pair<bool, qreal>, QPixmap> recycleBinIcons_;
  // hwnd -> unique id of the window's pinned app (PinManager.running_pinned).
  std::map<platform::WindowHandle, QString> runningPinned_;
  std::set<QString> pendingPinnedRecreations_;
  bool pinnedDisplayed_ = false;
  bool suspendUpdates_ = false;
  bool tooltipEnabled_;
  bool asyncRecycleBin_ = true;
};

void registerTaskbarWidget(WidgetRegistry& registry);

}  // namespace yasb::widgets
