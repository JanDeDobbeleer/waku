#pragma once

#include <QtQml/qqmlregistration.h>

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "platform/pin_support.h"
#include "platform/window_system.h"
#include "platform/window_tracker.h"
#include "widgets/taskbar/pin_store.h"
#include "widgets/taskbar/taskbar_icons.h"

class QQmlEngine;
class QJSEngine;
class QWindow;

namespace tidy::services {

namespace platform = ::yasb::platform;

// One window the tracker reports, as the service keeps it.
struct TaskbarWindow {
  platform::WindowHandle hwnd = 0;
  platform::WindowInfo info;  // including isActive and isFlashing as of the last tracker signal
  std::optional<QString> uniqueId;  // the app's pin id, read once when the window appears
  bool minimized = false;
};

// The windows of the desktop and the pinned apps, shared by every Taskbar widget of a `tidybar`
// run (Python's shared TaskbarWindowManager and PinManager). Owns the WindowTracker, the PinStore
// (taskbar_pinned.json, shared with the Widgets build) and the window icons, and performs the
// actions a button needs: focus, close, pin, launch. `TaskbarModel` turns its state into rows.
// Registers the image://windowicon provider on the engine that creates it.
//
// Threads: GUI thread only, except TaskbarIcons, which loads icons on its own one-thread pool and
// returns results as queued signals; it is joined when this object is destroyed.
//
// `available` is false when no tracker can run (offscreen platform, `setDisabled(true)`); the
// pinned apps still show and launch.
class TaskbarService : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(QString unavailableReason READ unavailableReason CONSTANT FINAL)

 public:
  // Everything a test replaces. A null `system` or `pins` is the real Win32 one.
  struct Deps {
    std::shared_ptr<platform::WindowSystem> system;
    std::shared_ptr<platform::PinSupport> pins;
    std::unique_ptr<platform::TrackerHooks> hooks;  // none in tests: events are fed by hand
    QString pinFile;                                // empty: PinStore::defaultFile()
    bool asyncIcons = true;
    bool trackerAvailable = true;
    QString unavailableReason;
  };

  // Real: the Win32 tracker; unavailable on the offscreen platform. Deliberately not
  // default-constructible: Qt uses a singleton's default constructor in preference to create(),
  // and create() must run to register the image provider.
  explicit TaskbarService(QObject* parent);
  explicit TaskbarService(Deps deps, QObject* parent = nullptr);
  ~TaskbarService() override;

  // QML_SINGLETON factory: builds the real one (or the test one) and registers the provider.
  static TaskbarService* create(QQmlEngine* engine, QJSEngine*);
  // Tests: create() builds the singleton with this factory instead of the real one.
  static void setFactoryForTests(std::function<std::unique_ptr<TaskbarService>()> factory);
  // `tidybar --no-taskbar` (once wired): the real service has no tracker.
  static void setDisabled(bool disabled);

  bool available() const { return tracker_ != nullptr; }
  QString unavailableReason() const { return reason_; }

  // Starts the tracker on this bar window's handle (the shell hook needs a top-level window),
  // once; later calls and a null window do nothing.
  Q_INVOKABLE void attach(QWindow* window);
  // A widget with showOnlyVisible=false keeps cloaked windows (Python only ever raises this).
  void keepCloakedTasks(bool keep);

  const std::vector<TaskbarWindow>& windows() const { return windows_; }
  const TaskbarWindow* window(platform::WindowHandle hwnd) const;
  yasb::widgets::PinStore& pinStore() { return *pinStore_; }
  const std::shared_ptr<platform::WindowSystem>& system() const { return system_; }

  // --- icons
  static constexpr int kIconSize = 32;
  // The icon of a window, or of a pinned app that does not run when `key` is a pseudo handle;
  // null while it loads (asynchronous loads bump the revision and emit changed()).
  QImage iconImage(platform::WindowHandle key);
  int iconRevision(platform::WindowHandle key) const;

  // --- pseudo handles of pinned apps that do not run
  static bool isPseudo(platform::WindowHandle key) { return (key >> 63) != 0; }
  platform::WindowHandle pseudoFor(const QString& uniqueId);

  // --- actions (nothing here throws)
  // Focus: restore when minimised, minimise when already active, else bring to the front.
  void activate(platform::WindowHandle hwnd);
  // The window after the active one among `members` (the first when none is active).
  void cycle(const std::vector<platform::WindowHandle>& members);
  void closeWindow(platform::WindowHandle hwnd);
  void launchPinned(const QString& uniqueId);
  // Pins the app of this window after the existing pinned apps.
  void pinWindow(platform::WindowHandle hwnd);
  void unpinApp(const QString& uniqueId);

  platform::WindowTracker* tracker() const { return tracker_; }

 signals:
  // Windows, activity, minimise state, pins or icons changed: models recompute.
  void changed();

 private:
  void init(Deps deps);
  void onAdded(platform::WindowHandle hwnd, const platform::WindowInfo& info);
  void onRemoved(platform::WindowHandle hwnd);
  void onUpdated(platform::WindowHandle hwnd, const platform::WindowInfo& info);
  void onMinimizeChanged(platform::WindowHandle hwnd, bool minimized);
  void onIconsChanged();
  TaskbarWindow* find(platform::WindowHandle hwnd);
  void bringToForeground(platform::WindowHandle hwnd);

  struct IconEntry {
    QImage image;
    int revision = 0;
  };

  std::shared_ptr<platform::WindowSystem> system_;
  std::shared_ptr<platform::PinSupport> pins_;
  platform::WindowTracker* tracker_ = nullptr;  // child of this; null when unavailable
  yasb::widgets::PinStore* pinStore_ = nullptr;
  yasb::widgets::TaskbarIcons* icons_ = nullptr;
  QString reason_;
  std::vector<TaskbarWindow> windows_;  // in the order the windows first appeared
  std::map<platform::WindowHandle, IconEntry> iconCache_;
  std::map<QString, platform::WindowHandle> pseudoByUniqueId_;
  std::map<platform::WindowHandle, QString> uniqueIdByPseudo_;
};

// image://windowicon/<key>/<revision>: the 32 px icon of a window (or pinned app) key. The
// revision only busts QML's image cache. Synchronous on the GUI thread, where the service lives.
class WindowIconProvider : public QQuickImageProvider {
 public:
  explicit WindowIconProvider(TaskbarService* service);
  QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

 private:
  QPointer<TaskbarService> service_;
};

}  // namespace tidy::services
