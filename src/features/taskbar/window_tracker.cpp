#include "features/taskbar/window_tracker.h"

#include <QDebug>
#include <QTimer>
#include <algorithm>
#include <exception>

namespace wabar::taskbar {
namespace {

// Runs `fn`, logging instead of letting anything escape into Qt (slots, timers, hook callbacks).
template <typename Fn>
void guarded(const char* what, Fn&& fn) {
  try {
    fn();
  } catch (const std::exception& error) {
    qCritical().noquote() << "Taskbar tracker:" << what << "failed:" << error.what();
  } catch (...) {
    qCritical().noquote() << "Taskbar tracker:" << what << "failed";
  }
}

}  // namespace

WindowTracker::WindowTracker(std::shared_ptr<WindowSystem> system,
                             std::unique_ptr<TrackerHooks> hooks, QObject* parent)
    : QObject(parent), system_(std::move(system)), hooks_(std::move(hooks)) {}

WindowTracker::~WindowTracker() { stop(); }

bool WindowTracker::start(WindowHandle shellHookWindow) {
  if (initialized_) return true;
  if (shellHookWindow == 0) {
    qWarning() << "Taskbar tracker needs a Qt top-level window handle for the shell hook";
    return false;
  }
  if (hooks_ && !hooks_->install(shellHookWindow, *this)) {
    qWarning() << "Taskbar tracker could not install its window hooks";
    return false;
  }
  initialized_ = true;
  enumerateExisting();
  return true;
}

void WindowTracker::stop() {
  if (!initialized_) return;
  for (auto& [hwnd, timer] : pendingUpdates_) {
    timer->stop();
    timer->deleteLater();
  }
  pendingUpdates_.clear();
  if (hooks_) hooks_->uninstall();
  initialized_ = false;
}

// --- snapshots

WindowInfo WindowTracker::asDict(WindowHandle hwnd, const Tracked& tracked) const {
  WindowInfo info = system_->query(hwnd);
  info.hwnd = hwnd;
  info.title = tracked.title;
  info.className = tracked.className;
  info.isActive = tracked.isActive;
  info.isFlashing = tracked.isFlashing;
  return info;
}

std::map<WindowHandle, WindowInfo> WindowTracker::windows() const {
  std::map<WindowHandle, WindowInfo> result;
  for (const auto& [hwnd, tracked] : windows_) result.emplace(hwnd, asDict(hwnd, tracked));
  return result;
}

std::optional<WindowInfo> WindowTracker::window(WindowHandle hwnd) const {
  const auto found = windows_.find(hwnd);
  if (found == windows_.end()) return std::nullopt;
  return asDict(hwnd, found->second);
}

bool WindowTracker::isActive(WindowHandle hwnd) const {
  const auto found = windows_.find(hwnd);
  return found != windows_.end() && found->second.isActive;
}

bool WindowTracker::isFlashing(WindowHandle hwnd) const {
  const auto found = windows_.find(hwnd);
  return found != windows_.end() && found->second.isFlashing;
}

bool WindowTracker::isTaskbarWindow(WindowHandle hwnd, const Tracked& tracked) const {
  return system_->isTaskbarWindow(asDict(hwnd, tracked));
}

bool WindowTracker::isUwp(const WindowInfo& info) const {
  const QString cls = info.className.trimmed();
  const QString process = info.processName.value_or(QString()).trimmed();
  return cls == QLatin1String("Windows.UI.Core.CoreWindow") ||
         cls == QLatin1String("ApplicationFrameWindow") ||
         process == QLatin1String("ApplicationFrameHost.exe");
}

// ApplicationWindow.update(): refresh the cached title and class.
bool WindowTracker::updateCached(Tracked& tracked, WindowHandle hwnd) const {
  const WindowInfo fresh = system_->query(hwnd);
  const bool changed = tracked.title != fresh.title || tracked.className != fresh.className;
  tracked.title = fresh.title;
  tracked.className = fresh.className;
  return changed;
}

// --- event entry points

void WindowTracker::handleShellHook(std::uintptr_t wparam, std::intptr_t lparam) {
  guarded("shell hook", [&] {
    const WindowHandle hwnd = lparam ? static_cast<WindowHandle>(lparam) : 0;
    switch (wparam) {
      case shell::kWindowCreated:
        onWindowCreated(hwnd);
        break;
      case shell::kWindowDestroyed:
      case shell::kEndTask:
        onWindowDestroyed(hwnd);
        break;
      case shell::kWindowActivated:
      case shell::kRudeAppActivated:
        onWindowActivated(hwnd);
        break;
      case shell::kRedraw:
        onWindowRedraw(hwnd);
        break;
      case shell::kFlash:
        onWindowFlash(hwnd);
        break;
      case shell::kWindowReplacing:
        if (isTracked(hwnd)) updateWindow(hwnd);
        break;
      case shell::kWindowReplaced:
        if (isTracked(hwnd)) removeWindow(hwnd);
        break;
      case shell::kMonitorChanged:
        onWindowMonitorChanged(hwnd);
        break;
      default:
        break;
    }
  });
}

void WindowTracker::handleWinEvent(unsigned eventType, WindowHandle hwnd, long idObject,
                                   long idChild) {
  if (hwnd == 0 || idObject != 0 || idChild != 0) return;
  switch (eventType) {
    case shell::kEventObjectUncloaked:
      QTimer::singleShot(100, this,
                         [this, hwnd] { guarded("uncloak", [&] { onWindowUncloaked(hwnd); }); });
      break;
    case shell::kEventObjectCloaked:
      QTimer::singleShot(50, this,
                         [this, hwnd] { guarded("cloak", [&] { onWindowCloaked(hwnd); }); });
      break;
    case shell::kEventObjectShow:
      QTimer::singleShot(0, this, [this, hwnd] { guarded("show", [&] { onWindowShow(hwnd); }); });
      break;
    case shell::kEventObjectHide:
      QTimer::singleShot(0, this, [this, hwnd] { guarded("hide", [&] { onWindowHide(hwnd); }); });
      break;
    case shell::kEventSystemMinimizeStart:
      QTimer::singleShot(0, this, [this, hwnd] { onWindowMinimized(hwnd, true); });
      break;
    case shell::kEventSystemMinimizeEnd:
      QTimer::singleShot(0, this, [this, hwnd] { onWindowMinimized(hwnd, false); });
      break;
    default:
      if (isTracked(hwnd)) {
        QTimer::singleShot(0, this,
                           [this, hwnd] { guarded("update", [&] { updateWindow(hwnd); }); });
      }
      break;
  }
}

// --- handlers (one per _on_* of the Python manager)

void WindowTracker::onWindowCreated(WindowHandle hwnd) {
  if (isTracked(hwnd))
    updateWindow(hwnd);
  else
    addWindow(hwnd);
}

void WindowTracker::onWindowDestroyed(WindowHandle hwnd) {
  if (isTracked(hwnd)) removeWindow(hwnd);
}

void WindowTracker::onWindowMinimized(WindowHandle hwnd, bool minimized) {
  if (isTracked(hwnd)) emit windowMinimizeChanged(hwnd, minimized);
}

void WindowTracker::onWindowActivated(WindowHandle hwnd) {
  const WindowHandle foreground = system_->foregroundWindow();
  if (hwnd == 0 && foreground != 0) hwnd = foreground;

  std::vector<WindowHandle> toUpdate;
  const auto mark = [&](WindowHandle handle) {
    if (std::find(toUpdate.begin(), toUpdate.end(), handle) == toUpdate.end())
      toUpdate.push_back(handle);
  };
  std::map<WindowHandle, bool> oldStates;
  for (const auto& [handle, tracked] : windows_) oldStates[handle] = tracked.isActive;
  for (auto& [handle, tracked] : windows_) {
    tracked.isActive = false;
    if (oldStates[handle]) mark(handle);
  }

  const auto activate = [&](WindowHandle handle) {
    Tracked& tracked = windows_[handle];
    tracked.isActive = true;
    if (!oldStates[handle]) mark(handle);
    if (tracked.isFlashing) {
      tracked.isFlashing = false;
      mark(handle);
    }
  };
  if (hwnd != 0 && isTracked(hwnd)) {
    activate(hwnd);
  } else if (hwnd != 0) {
    addWindow(hwnd, /*active=*/true);
  } else if (foreground != 0 && isTracked(foreground)) {
    activate(foreground);
  }

  for (WindowHandle handle : toUpdate) {
    const auto found = windows_.find(handle);
    if (found != windows_.end()) emit windowUpdated(handle, asDict(handle, found->second));
  }
}

void WindowTracker::onWindowRedraw(WindowHandle hwnd) {
  if (isTracked(hwnd)) {
    // Debounced so an icon is not read too early (Explorer folder switch).
    debounceUpdate(hwnd, 50);
  } else {
    addWindow(hwnd);
  }
}

void WindowTracker::onWindowFlash(WindowHandle hwnd) {
  const auto found = windows_.find(hwnd);
  if (found == windows_.end()) {
    addWindow(hwnd, false, /*flashing=*/true);
    return;
  }
  const bool wasFlashing = found->second.isFlashing;
  found->second.isFlashing = true;
  if (!wasFlashing)
    emit windowUpdated(hwnd, asDict(hwnd, found->second));
  else
    updateWindow(hwnd);
}

void WindowTracker::onWindowMonitorChanged(WindowHandle hwnd) {
  const auto found = windows_.find(hwnd);
  if (found == windows_.end()) {
    delayedAdd(hwnd, 50);
    return;
  }
  const WindowInfo oldData = asDict(hwnd, found->second);
  updateCached(found->second, hwnd);
  const WindowInfo newData = asDict(hwnd, found->second);
  emit windowMonitorChanged(hwnd, newData);
  if (!(oldData == newData)) emit windowUpdated(hwnd, newData);
}

void WindowTracker::onWindowCloaked(WindowHandle hwnd) {
  const auto found = windows_.find(hwnd);
  if (found == windows_.end()) return;
  updateCached(found->second, hwnd);
  const WindowInfo info = asDict(hwnd, found->second);
  // Cloaked UWP frames are transient: keep and refresh them and let the widgets filter.
  if (keepCloaked_ || isUwp(info)) {
    emit windowUpdated(hwnd, info);
  } else {
    removeWindow(hwnd);
  }
}

void WindowTracker::onWindowUncloaked(WindowHandle hwnd) {
  const auto found = windows_.find(hwnd);
  if (found == windows_.end()) {
    addWindow(hwnd);
    return;
  }
  // Force-emit so widgets that removed it while cloaked can add it again.
  updateCached(found->second, hwnd);
  emit windowUpdated(hwnd, asDict(hwnd, found->second));
}

void WindowTracker::onWindowShow(WindowHandle hwnd) {
  if (isTracked(hwnd))
    updateWindow(hwnd);
  else
    addWindow(hwnd);
}

void WindowTracker::onWindowHide(WindowHandle hwnd) {
  if (isTracked(hwnd)) removeWindow(hwnd);
}

// --- bookkeeping

void WindowTracker::addWindow(WindowHandle hwnd, bool active, bool flashing) {
  if (!system_->isWindow(hwnd)) return;
  const WindowInfo fresh = system_->query(hwnd);
  Tracked tracked;
  tracked.title = fresh.title;
  tracked.className = fresh.className;
  if (!isTaskbarWindow(hwnd, tracked)) {
    // UWP apps sometimes need a moment before they qualify.
    if (fresh.className == QLatin1String("ApplicationFrameWindow") ||
        fresh.className == QLatin1String("Windows.UI.Core.CoreWindow")) {
      QTimer::singleShot(500, this,
                         [this, hwnd] { guarded("uwp check", [&] { delayedAdd(hwnd, 0); }); });
    }
    return;
  }
  tracked.isActive = active;
  tracked.isFlashing = flashing;
  windows_[hwnd] = tracked;
  emit windowAdded(hwnd, asDict(hwnd, tracked));
}

void WindowTracker::delayedAdd(WindowHandle hwnd, int delayMs) {
  const auto attempt = [this, hwnd] {
    guarded("delayed add", [&] {
      if (isTracked(hwnd) || !system_->isWindow(hwnd)) return;
      const WindowInfo fresh = system_->query(hwnd);
      Tracked tracked;
      tracked.title = fresh.title;
      tracked.className = fresh.className;
      if (!isTaskbarWindow(hwnd, tracked)) return;
      windows_[hwnd] = tracked;
      emit windowAdded(hwnd, asDict(hwnd, tracked));
    });
  };
  if (delayMs <= 0)
    attempt();
  else
    QTimer::singleShot(delayMs, this, attempt);
}

void WindowTracker::removeWindow(WindowHandle hwnd) {
  const auto found = windows_.find(hwnd);
  if (found == windows_.end()) return;
  const WindowInfo info = asDict(hwnd, found->second);
  windows_.erase(found);
  emit windowRemoved(hwnd, info);
}

void WindowTracker::updateWindow(WindowHandle hwnd) {
  const auto found = windows_.find(hwnd);
  if (found == windows_.end()) return;
  const WindowInfo oldData = asDict(hwnd, found->second);
  updateCached(found->second, hwnd);

  if (!isTaskbarWindow(hwnd, found->second)) {
    const WindowInfo info = asDict(hwnd, found->second);
    // UWP windows survive transient transitions, and cloaked ones stay when asked to.
    if (isUwp(info) || (info.isCloaked && keepCloaked_)) {
      emit windowUpdated(hwnd, info);
      return;
    }
    removeWindow(hwnd);
    return;
  }
  const WindowInfo newData = asDict(hwnd, found->second);
  if (!(oldData == newData)) emit windowUpdated(hwnd, newData);
}

void WindowTracker::debounceUpdate(WindowHandle hwnd, int delayMs) {
  if (const auto existing = pendingUpdates_.find(hwnd); existing != pendingUpdates_.end()) {
    existing->second->stop();
    existing->second->deleteLater();
    pendingUpdates_.erase(existing);
  }
  auto* timer = new QTimer(this);
  timer->setSingleShot(true);
  connect(timer, &QTimer::timeout, this, [this, hwnd, timer] {
    pendingUpdates_.erase(hwnd);
    timer->deleteLater();
    guarded("debounced update", [&] { updateWindow(hwnd); });
  });
  pendingUpdates_[hwnd] = timer;
  timer->start(std::max(0, delayMs));
}

void WindowTracker::enumerateExisting() {
  guarded("enumerate", [&] {
    for (WindowHandle hwnd : system_->enumerateWindows()) {
      guarded("add existing window", [&] { addWindow(hwnd); });
    }
    const WindowHandle foreground = system_->foregroundWindow();
    if (const auto found = windows_.find(foreground); foreground != 0 && found != windows_.end())
      found->second.isActive = true;
  });
}

void WindowTracker::rescanWindows() {
  windows_.clear();
  enumerateExisting();
}

}  // namespace wabar::taskbar
