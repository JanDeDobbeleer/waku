#include "features/taskbar/qml/taskbar_service.h"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <exception>
#include <utility>

#include "kernel/qt/live_desktop.h"

namespace waku::taskbar {
namespace {

std::function<std::unique_ptr<TaskbarService>()>& testFactory() {
  static std::function<std::unique_ptr<TaskbarService>()> factory;
  return factory;
}

// Runs `fn`, logging instead of letting anything escape into Qt.
template <typename Fn>
void guarded(const char* what, Fn&& fn) {
  try {
    fn();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Taskbar:" << what << "failed:" << error.what();
  } catch (...) {
    qWarning().noquote() << "Taskbar:" << what << "failed";
  }
}

constexpr WindowHandle kPseudoBase = WindowHandle(1) << 63;

// Applications cache the progress receiver they found once (Windows Terminal, per window, when it
// is made), so it lives as long as the process: a reload deletes the Taskbar service and makes a
// new one, and a receiver that went with it would leave every running application sending into a
// window that no longer exists. Stopped when the application quits.
struct SharedProgress {
  std::shared_ptr<ProgressCache> cache;
  std::unique_ptr<ProgressReceiver> receiver;
};

std::shared_ptr<ProgressCache> sharedProgress() {
  static SharedProgress* shared = nullptr;
  if (shared) return shared->cache;
  auto cache = std::make_shared<ProgressCache>();
  auto receiver = makeWin32ProgressReceiver(cache);
  if (!receiver) return cache;
  shared = new SharedProgress{cache, std::move(receiver)};
  QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
                   QCoreApplication::instance(), [] { delete std::exchange(shared, nullptr); });
  return cache;
}

}  // namespace

TaskbarService::TaskbarService(QObject* parent) : QObject(parent) {
  Deps deps;
  if (kernel::capabilityDisabled(QStringLiteral("taskbar"))) {
    deps.trackerAvailable = false;
    deps.unavailableReason = QStringLiteral("taskbar unavailable: disabled by --no-taskbar");
  } else if (!kernel::liveDesktop()) {
    deps.trackerAvailable = false;
    deps.unavailableReason = QStringLiteral("taskbar unavailable: offscreen platform");
  } else {
    deps.hooks = makeWin32TrackerHooks();
    deps.progress = sharedProgress();
  }
  init(std::move(deps));
}

TaskbarService::TaskbarService(Deps deps, QObject* parent) : QObject(parent) {
  init(std::move(deps));
}

TaskbarService::~TaskbarService() {
  if (tracker_) tracker_->stop();
  iconStore_->clear();
}

void TaskbarService::init(Deps deps) {
  reason_ = std::move(deps.unavailableReason);
  system_ = deps.system ? std::move(deps.system) : makeWin32WindowSystem();
  pins_ = deps.pins ? std::move(deps.pins) : makeWin32PinSupport();
  pinStore_ = new waku::taskbar::PinStore(
      deps.pinFile.isEmpty() ? waku::taskbar::PinStore::defaultFile() : deps.pinFile, this);
  pinStore_->load();
  icons_ = new waku::taskbar::TaskbarIcons(system_, deps.asyncIcons, this);
  connect(icons_, &waku::taskbar::TaskbarIcons::iconsChanged, this,
          [this] { guarded("icons", [&] { onIconsChanged(); }); });
  connect(pinStore_, &waku::taskbar::PinStore::pinnedAppsChanged, this,
          [this] { scheduleChanged(); });

  if (!deps.trackerAvailable) return;
  tracker_ = new WindowTracker(system_, std::move(deps.hooks), this);
  tracker_->setProgress(std::move(deps.progress));
  connect(tracker_, &WindowTracker::windowAdded, this,
          [this](WindowHandle hwnd, const WindowInfo& info) {
            guarded("window added", [&] { onAdded(hwnd, info); });
          });
  connect(tracker_, &WindowTracker::windowRemoved, this,
          [this](WindowHandle hwnd, const WindowInfo&) {
            guarded("window removed", [&] { onRemoved(hwnd); });
          });
  connect(tracker_, &WindowTracker::windowUpdated, this,
          [this](WindowHandle hwnd, const WindowInfo& info) {
            guarded("window updated", [&] { onUpdated(hwnd, info); });
          });
  connect(tracker_, &WindowTracker::windowMonitorChanged, this,
          [this](WindowHandle hwnd, const WindowInfo& info) {
            guarded("window monitor", [&] { onUpdated(hwnd, info); });
          });
  connect(tracker_, &WindowTracker::windowMinimizeChanged, this,
          [this](WindowHandle hwnd, bool minimized) {
            guarded("window minimize", [&] { onMinimizeChanged(hwnd, minimized); });
          });
  connect(tracker_, &WindowTracker::windowProgressChanged, this,
          [this](WindowHandle hwnd, const WindowProgress& progress) {
            guarded("window progress", [&] {
              if (TaskbarWindow* w = find(hwnd)) {
                w->info.progress = progress;
                scheduleChanged();
              }
            });
          });
}

TaskbarService* TaskbarService::create(QQmlEngine* engine, QJSEngine*) {
  // The engine owns the returned singleton and deletes its providers first.
  TaskbarService* service = nullptr;
  if (testFactory()) service = testFactory()().release();
  if (!service) service = new TaskbarService(nullptr);
  if (engine) {
    engine->addImageProvider(QStringLiteral("windowicon"), new WindowIconProvider(service));
  }
  return service;
}

void TaskbarService::setFactoryForTests(std::function<std::unique_ptr<TaskbarService>()> factory) {
  testFactory() = std::move(factory);
}

void TaskbarService::attach(QWindow* window) {
  if (!tracker_ || !window || tracker_->isInitialized()) return;
  guarded("attach", [&] {
    if (!tracker_->start(static_cast<WindowHandle>(window->winId())))
      qWarning() << "Taskbar could not start the window tracker; it will stay empty";
  });
}

void TaskbarService::keepCloakedTasks(bool keep) {
  if (tracker_) tracker_->keepCloakedTasks(keep);
}

const TaskbarWindow* TaskbarService::window(WindowHandle hwnd) const {
  for (const TaskbarWindow& w : windows_) {
    if (w.hwnd == hwnd) return &w;
  }
  return nullptr;
}

TaskbarWindow* TaskbarService::find(WindowHandle hwnd) {
  for (TaskbarWindow& w : windows_) {
    if (w.hwnd == hwnd) return &w;
  }
  return nullptr;
}

// --- tracker signals

void TaskbarService::onAdded(WindowHandle hwnd, const WindowInfo& info) {
  if (TaskbarWindow* existing = find(hwnd)) {
    existing->info = info;
    scheduleChanged();
    return;
  }
  // For Explorer windows appUniqueId makes out-of-process COM calls, and the GUI thread's STA
  // pumps messages while it waits: a nested onAdded/onUpdated/onRemoved for this very window can
  // run inside the call. So look again afterwards instead of trusting the find() above.
  std::optional<QString> uniqueId = system_->appUniqueId(info);
  const bool minimized = system_->isIconic(hwnd);
  if (TaskbarWindow* nested = find(hwnd)) {
    if (!nested->uniqueId) nested->uniqueId = std::move(uniqueId);
  } else if (system_->isWindow(hwnd)) {  // it may have closed meanwhile
    TaskbarWindow w;
    w.hwnd = hwnd;
    w.info = info;
    w.uniqueId = std::move(uniqueId);
    w.minimized = minimized;
    windows_.push_back(std::move(w));
  }
  scheduleChanged();
}

void TaskbarService::onRemoved(WindowHandle hwnd) {
  const auto it = std::find_if(windows_.begin(), windows_.end(),
                               [hwnd](const TaskbarWindow& w) { return w.hwnd == hwnd; });
  if (it == windows_.end()) return;
  windows_.erase(it);
  iconCache_.erase(hwnd);
  iconStore_->remove(hwnd);
  icons_->forget(hwnd);  // the handle may be reused
  scheduleChanged();
}

void TaskbarService::onUpdated(WindowHandle hwnd, const WindowInfo& info) {
  TaskbarWindow* w = find(hwnd);
  if (!w) {
    onAdded(hwnd, info);
    return;
  }
  w->info = info;
  // A focused window is not minimised.
  if (info.isActive) w->minimized = false;
  scheduleChanged();
}

void TaskbarService::onMinimizeChanged(WindowHandle hwnd, bool minimized) {
  TaskbarWindow* w = find(hwnd);
  if (!w || w->minimized == minimized) return;
  w->minimized = minimized;
  scheduleChanged();
}

// One changed() per event-loop turn: an activation updates the old and the new window, and the
// models rebuild every row on each emission.
void TaskbarService::scheduleChanged() {
  if (changePending_) return;
  changePending_ = true;
  QTimer::singleShot(0, this, [this] {
    changePending_ = false;
    emit changed();
  });
}

// --- icons

QImage TaskbarService::iconImage(WindowHandle key) {
  IconEntry& entry = iconCache_[key];
  if (!entry.image.isNull()) return entry.image;
  // The loader's cached image itself: shared, not copied through a QPixmap and back.
  QImage image;
  try {
    if (isPseudo(key)) {
      const auto id = uniqueIdByPseudo_.find(key);
      if (id != uniqueIdByPseudo_.end()) {
        const QString uniqueId = id->second;
        const PinMetadata metadata{pinStore_->title(uniqueId), pinStore_->shortcutPath(uniqueId)};
        const std::shared_ptr<PinSupport> pins = pins_;
        image = icons_->imageFor(key, kIconSize, 1.0, [pins, uniqueId, metadata](int size) {
          return waku::taskbar::loadPinnedIcon(*pins, uniqueId, metadata, size);
        });
      }
    } else {
      image = icons_->image(key, kIconSize, 1.0);
    }
  } catch (const std::exception& error) {
    qDebug().noquote() << "Taskbar: icon of" << key << "failed:" << error.what();
  }
  if (!image.isNull()) {
    entry.image = image;
    ++entry.revision;
    iconStore_->set(key, entry.image);
  }
  return entry.image;
}

int TaskbarService::iconRevision(WindowHandle key) const {
  const auto found = iconCache_.find(key);
  return found == iconCache_.end() ? 0 : found->second.revision;
}

void TaskbarService::onIconsChanged() {
  bool any = false;
  for (auto& [key, entry] : iconCache_) {
    if (!entry.image.isNull()) continue;
    const int before = entry.revision;
    iconImage(key);
    any = any || entry.revision != before;
  }
  if (any) scheduleChanged();
}

WindowHandle TaskbarService::pseudoFor(const QString& uniqueId) {
  const auto found = pseudoByUniqueId_.find(uniqueId);
  if (found != pseudoByUniqueId_.end()) return found->second;
  const WindowHandle pseudo = kPseudoBase | (pseudoByUniqueId_.size() + 1);
  pseudoByUniqueId_[uniqueId] = pseudo;
  uniqueIdByPseudo_[pseudo] = uniqueId;
  return pseudo;
}

// --- actions

void TaskbarService::activate(WindowHandle hwnd) {
  if (isPseudo(hwnd)) return;
  bringToForeground(hwnd);
}

// Port of TaskbarWidget::bringToForeground without the preview and flash timers.
void TaskbarService::bringToForeground(WindowHandle hwnd) {
  guarded("activate", [&] {
    if (!system_->isWindow(hwnd)) return;
    const auto [base, focusTarget] = system_->resolveBaseAndFocus(hwnd);
    const TaskbarWindow* tracked = window(base);
    const bool isActive = (tracked && tracked->info.isActive) || system_->isOwnerRootActive(base);

    if (system_->isIconic(base)) {
      system_->restoreWindow(base);
      system_->setForeground(base);
      const WindowHandle target = focusTarget ? focusTarget : base;
      QPointer<TaskbarService> self(this);
      QTimer::singleShot(0, this, [self, target] {
        if (self) guarded("focus", [&] { self->system_->setForeground(target); });
      });
      return;
    }
    if (isActive) {
      if (system_->canMinimize(hwnd)) system_->minimizeWindow(base);
      return;
    }
    system_->showWindow(base);
    system_->setForeground(base);
  });
}

void TaskbarService::cycle(const std::vector<WindowHandle>& members) {
  if (members.empty()) return;
  int active = -1;
  for (size_t i = 0; i < members.size(); ++i) {
    const TaskbarWindow* w = window(members[i]);
    if (w && w->info.isActive) {
      active = static_cast<int>(i);
      break;
    }
  }
  activate(members[static_cast<size_t>(active + 1) % members.size()]);
}

void TaskbarService::closeWindow(WindowHandle hwnd) {
  if (isPseudo(hwnd)) return;
  guarded("close", [&] {
    if (system_->isWindow(hwnd)) system_->closeApplication(hwnd, false);
  });
}

void TaskbarService::launchPinned(const QString& uniqueId) {
  guarded("launch", [&] {
    const PinMetadata metadata{pinStore_->title(uniqueId), pinStore_->shortcutPath(uniqueId)};
    pins_->launch(uniqueId, metadata, QString());
  });
}

void TaskbarService::pinWindow(WindowHandle hwnd) {
  guarded("pin", [&] {
    const TaskbarWindow* w = window(hwnd);
    if (!w || !w->uniqueId) {
      qWarning() << "Taskbar: cannot pin, the app could not be identified";
      return;
    }
    PinMetadata metadata = pins_->resolveMetadata(w->info, *w->uniqueId);
    if (metadata.title.isEmpty()) metadata.title = w->info.title;
    QJsonObject json{{QStringLiteral("title"), metadata.title}};
    if (!metadata.shortcutPath.isEmpty())
      json.insert(QStringLiteral("shortcut_path"), metadata.shortcutPath);
    pinStore_->load();
    // After the existing pinned apps, before the unpinned running ones.
    pinStore_->pin(*w->uniqueId, json, static_cast<int>(pinStore_->order().size()));
  });
}

void TaskbarService::unpinApp(const QString& uniqueId) {
  guarded("unpin", [&] {
    pinStore_->load();
    pinStore_->unpin(uniqueId);
  });
}

// --- image provider

WindowIconProvider::WindowIconProvider(TaskbarService* service)
    : QQuickImageProvider(QQuickImageProvider::Image),
      store_(service ? service->iconStore() : nullptr) {}

QImage WindowIconProvider::requestImage(const QString& id, QSize* size,
                                        const QSize& /*requestedSize*/) {
  if (size) *size = QSize(0, 0);
  if (!store_) return {};
  bool ok = false;
  const WindowHandle key = id.section(QLatin1Char('/'), 0, 0).toULongLong(&ok);
  if (!ok) return {};
  const QImage image = store_->image(key);
  if (image.isNull()) return {};
  if (size) *size = image.size();
  return image;
}

}  // namespace waku::taskbar
