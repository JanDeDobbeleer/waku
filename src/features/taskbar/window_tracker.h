#pragma once

#include <QObject>
#include <map>
#include <memory>

#include "features/taskbar/window_system.h"

class QTimer;

namespace waku::taskbar {

// Shell hook (HSHELL_*) and WinEvent codes the tracker reacts to.
namespace shell {
constexpr std::uintptr_t kWindowCreated = 1;
constexpr std::uintptr_t kWindowDestroyed = 2;
constexpr std::uintptr_t kWindowActivated = 4;
constexpr std::uintptr_t kRedraw = 6;
constexpr std::uintptr_t kEndTask = 10;
constexpr std::uintptr_t kWindowReplaced = 13;
constexpr std::uintptr_t kWindowReplacing = 14;
constexpr std::uintptr_t kMonitorChanged = 16;
constexpr std::uintptr_t kRudeAppActivated = 32772;
constexpr std::uintptr_t kFlash = 32774;

constexpr unsigned kEventSystemMinimizeStart = 0x0016;
constexpr unsigned kEventSystemMinimizeEnd = 0x0017;
constexpr unsigned kEventObjectShow = 0x8002;
constexpr unsigned kEventObjectHide = 0x8003;
constexpr unsigned kEventObjectCloaked = 0x8017;
constexpr unsigned kEventObjectUncloaked = 0x8018;
}  // namespace shell

class WindowTracker;

// Installs the OS notification sources (shell hook on a window, WinEvent hooks) and forwards them
// to WindowTracker::handleShellHook / handleWinEvent. Absent in tests, which call those directly.
class TrackerHooks {
 public:
  virtual ~TrackerHooks() = default;
  virtual bool install(WindowHandle shellHookWindow, WindowTracker& tracker) = 0;
  virtual void uninstall() = 0;
};

std::unique_ptr<TrackerHooks> makeWin32TrackerHooks();

// Port of TaskbarWindowManager: tracks the top-level windows that belong on a taskbar and emits
// signals when they appear, change, vanish, move monitors or get minimized. One instance is shared
// by every taskbar widget (PollingWorkerRegistry service "taskbar_windows"). GUI thread only.
class WindowTracker : public QObject {
  Q_OBJECT

 public:
  WindowTracker(std::shared_ptr<WindowSystem> system, std::unique_ptr<TrackerHooks> hooks = {},
                QObject* parent = nullptr);
  ~WindowTracker() override;

  // Registers the hooks on `shellHookWindow` (a Qt top-level window) and enumerates the open
  // windows (their windowAdded signals fire synchronously). False when the hooks could not be
  // installed; the tracker then stays stopped.
  bool start(WindowHandle shellHookWindow);
  Q_INVOKABLE void stop();
  bool isInitialized() const { return initialized_; }

  // Python only ever raises this: a widget with show_only_visible=false keeps cloaked tasks and the
  // others filter on their own.
  void keepCloakedTasks(bool keep) { keepCloaked_ = keepCloaked_ || keep; }
  bool keepsCloakedTasks() const { return keepCloaked_; }

  const std::shared_ptr<WindowSystem>& system() const { return system_; }

  std::map<WindowHandle, WindowInfo> windows() const;
  std::optional<WindowInfo> window(WindowHandle hwnd) const;
  bool isTracked(WindowHandle hwnd) const { return windows_.count(hwnd) != 0; }
  bool isActive(WindowHandle hwnd) const;
  bool isFlashing(WindowHandle hwnd) const;

  // Forget every tracked window and enumerate again.
  void rescanWindows();

  // Entry points for the OS hooks (and tests). Nothing here throws.
  void handleShellHook(std::uintptr_t wparam, std::intptr_t lparam);
  void handleWinEvent(unsigned eventType, WindowHandle hwnd, long idObject, long idChild);

 signals:
  void windowAdded(waku::taskbar::WindowHandle hwnd, const waku::taskbar::WindowInfo& info);
  void windowRemoved(waku::taskbar::WindowHandle hwnd, const waku::taskbar::WindowInfo& info);
  void windowUpdated(waku::taskbar::WindowHandle hwnd, const waku::taskbar::WindowInfo& info);
  void windowMonitorChanged(waku::taskbar::WindowHandle hwnd,
                            const waku::taskbar::WindowInfo& info);
  void windowMinimizeChanged(waku::taskbar::WindowHandle hwnd, bool minimized);

 private:
  // ApplicationWindow: the title and class are cached and only refreshed by update().
  struct Tracked {
    QString title;
    QString className;
    bool isActive = false;
    bool isFlashing = false;
  };

  // A fresh query with the tracked fields on top.
  WindowInfo asDict(WindowHandle hwnd, const Tracked& tracked) const;
  // `fresh` (one query of the event) with the tracked fields on top.
  static WindowInfo merged(WindowHandle hwnd, const Tracked& tracked, WindowInfo fresh);
  bool isUwp(const WindowInfo& info) const;
  static bool isUwpClass(const QString& className);
  // ApplicationWindow.update(): copies the title and class of `fresh` into the cache.
  static bool updateCached(Tracked& tracked, const WindowInfo& fresh);

  void onWindowCreated(WindowHandle hwnd);
  void onWindowDestroyed(WindowHandle hwnd);
  void onWindowMinimized(WindowHandle hwnd, bool minimized);
  void onWindowActivated(WindowHandle hwnd);
  void onWindowRedraw(WindowHandle hwnd);
  void onWindowFlash(WindowHandle hwnd);
  void onWindowMonitorChanged(WindowHandle hwnd);
  void onWindowCloaked(WindowHandle hwnd);
  void onWindowUncloaked(WindowHandle hwnd);
  void onWindowShow(WindowHandle hwnd);
  void onWindowHide(WindowHandle hwnd);

  void addWindow(WindowHandle hwnd, bool active = false, bool flashing = false);
  void delayedAdd(WindowHandle hwnd, int delayMs);
  void removeWindow(WindowHandle hwnd);
  void updateWindow(WindowHandle hwnd);
  void debounceUpdate(WindowHandle hwnd, int delayMs);
  void enumerateExisting();

  std::shared_ptr<WindowSystem> system_;
  std::unique_ptr<TrackerHooks> hooks_;
  std::map<WindowHandle, Tracked> windows_;
  std::map<WindowHandle, QTimer*> pendingUpdates_;
  bool initialized_ = false;
  bool keepCloaked_ = false;
};

}  // namespace waku::taskbar
