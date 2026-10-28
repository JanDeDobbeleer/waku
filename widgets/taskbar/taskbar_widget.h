#pragma once

#include <QBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QPropertyAnimation>
#include <QTimer>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "core/widgets/base_widget.h"
#include "core/widgets/registry.h"
#include "platform/dwm_thumbnail.h"
#include "platform/window_system.h"
#include "platform/window_tracker.h"

namespace yasb::bar {
class GlobalState;
}

class QMenu;

namespace yasb::widgets {

class TaskbarWidget;
class TaskbarIcons;
class PreviewManager;

// Port of DraggableAppButton minus the drag and drop (which only reorders pinned apps): one
// taskbar button, shared by every window of an app when grouping is on.
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
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void checkHidePreview();
  void onPreviewTimeout();

  TaskbarWidget* taskbar_;
  platform::WindowHandle hwnd_;
  QTimer previewTimer_;
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
};

// yasb.taskbar.TaskbarWidget: one button per window (or per app with grouping), a count badge,
// animated add and remove, attention flashing and the hover preview with peek.
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
};

void registerTaskbarWidget(WidgetRegistry& registry);

}  // namespace yasb::widgets
