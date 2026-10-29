#include "spike/services/taskbar_service.h"

#include <QDebug>
#include <QGuiApplication>
#include <QJsonObject>
#include <QPixmap>
#include <QQmlEngine>
#include <QTimer>
#include <QWindow>
#include <algorithm>
#include <exception>
#include <utility>

namespace tidy::services {
namespace {

bool g_disabled = false;

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

constexpr platform::WindowHandle kPseudoBase = platform::WindowHandle(1) << 63;

}  // namespace

TaskbarService::TaskbarService(QObject* parent) : QObject(parent) {
  Deps deps;
  if (g_disabled) {
    deps.trackerAvailable = false;
    deps.unavailableReason = QStringLiteral("taskbar unavailable: disabled by --no-taskbar");
  } else if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
    deps.trackerAvailable = false;
    deps.unavailableReason = QStringLiteral("taskbar unavailable: offscreen platform");
  } else {
    deps.hooks = platform::makeWin32TrackerHooks();
  }
  init(std::move(deps));
}

TaskbarService::TaskbarService(Deps deps, QObject* parent) : QObject(parent) {
  init(std::move(deps));
}

TaskbarService::~TaskbarService() {
  if (tracker_) tracker_->stop();
}

void TaskbarService::init(Deps deps) {
  reason_ = std::move(deps.unavailableReason);
  system_ = deps.system ? std::move(deps.system) : platform::makeWin32WindowSystem();
  pins_ = deps.pins ? std::move(deps.pins) : platform::makeWin32PinSupport();
  pinStore_ = new yasb::widgets::PinStore(
      deps.pinFile.isEmpty() ? yasb::widgets::PinStore::defaultFile() : deps.pinFile, this);
  pinStore_->load();
  icons_ = new yasb::widgets::TaskbarIcons(system_, deps.asyncIcons, this);
  connect(icons_, &yasb::widgets::TaskbarIcons::iconsChanged, this,
          [this] { guarded("icons", [&] { onIconsChanged(); }); });
  connect(pinStore_, &yasb::widgets::PinStore::pinnedAppsChanged, this, [this] { emit changed(); });

  if (!deps.trackerAvailable) return;
  tracker_ = new platform::WindowTracker(system_, std::move(deps.hooks), this);
  connect(tracker_, &platform::WindowTracker::windowAdded, this,
          [this](platform::WindowHandle hwnd, const platform::WindowInfo& info) {
            guarded("window added", [&] { onAdded(hwnd, info); });
          });
  connect(tracker_, &platform::WindowTracker::windowRemoved, this,
          [this](platform::WindowHandle hwnd, const platform::WindowInfo&) {
            guarded("window removed", [&] { onRemoved(hwnd); });
          });
  connect(tracker_, &platform::WindowTracker::windowUpdated, this,
          [this](platform::WindowHandle hwnd, const platform::WindowInfo& info) {
            guarded("window updated", [&] { onUpdated(hwnd, info); });
          });
  connect(tracker_, &platform::WindowTracker::windowMonitorChanged, this,
          [this](platform::WindowHandle hwnd, const platform::WindowInfo& info) {
            guarded("window monitor", [&] { onUpdated(hwnd, info); });
          });
  connect(tracker_, &platform::WindowTracker::windowMinimizeChanged, this,
          [this](platform::WindowHandle hwnd, bool minimized) {
            guarded("window minimize", [&] { onMinimizeChanged(hwnd, minimized); });
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

void TaskbarService::setDisabled(bool disabled) { g_disabled = disabled; }

void TaskbarService::attach(QWindow* window) {
  if (!tracker_ || !window || tracker_->isInitialized()) return;
  guarded("attach", [&] {
    if (!tracker_->start(static_cast<platform::WindowHandle>(window->winId())))
      qWarning() << "Taskbar could not start the window tracker; it will stay empty";
  });
}

void TaskbarService::keepCloakedTasks(bool keep) {
  if (tracker_) tracker_->keepCloakedTasks(keep);
}

const TaskbarWindow* TaskbarService::window(platform::WindowHandle hwnd) const {
  for (const TaskbarWindow& w : windows_) {
    if (w.hwnd == hwnd) return &w;
  }
  return nullptr;
}

TaskbarWindow* TaskbarService::find(platform::WindowHandle hwnd) {
  for (TaskbarWindow& w : windows_) {
    if (w.hwnd == hwnd) return &w;
  }
  return nullptr;
}

// --- tracker signals

void TaskbarService::onAdded(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  if (TaskbarWindow* existing = find(hwnd)) {
    existing->info = info;
  } else {
    TaskbarWindow w;
    w.hwnd = hwnd;
    w.info = info;
    w.uniqueId = system_->appUniqueId(info);
    w.minimized = system_->isIconic(hwnd);
    windows_.push_back(std::move(w));
  }
  emit changed();
}

void TaskbarService::onRemoved(platform::WindowHandle hwnd) {
  const auto it = std::find_if(windows_.begin(), windows_.end(),
                               [hwnd](const TaskbarWindow& w) { return w.hwnd == hwnd; });
  if (it == windows_.end()) return;
  windows_.erase(it);
  iconCache_.erase(hwnd);
  icons_->forget(hwnd);  // the handle may be reused
  emit changed();
}

void TaskbarService::onUpdated(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  TaskbarWindow* w = find(hwnd);
  if (!w) {
    onAdded(hwnd, info);
    return;
  }
  w->info = info;
  // A focused window is not minimised.
  if (info.isActive) w->minimized = false;
  emit changed();
}

void TaskbarService::onMinimizeChanged(platform::WindowHandle hwnd, bool minimized) {
  TaskbarWindow* w = find(hwnd);
  if (!w || w->minimized == minimized) return;
  w->minimized = minimized;
  emit changed();
}

// --- icons

QImage TaskbarService::iconImage(platform::WindowHandle key) {
  IconEntry& entry = iconCache_[key];
  if (!entry.image.isNull()) return entry.image;
  QPixmap pixmap;
  try {
    if (isPseudo(key)) {
      const auto id = uniqueIdByPseudo_.find(key);
      if (id != uniqueIdByPseudo_.end()) {
        const QString uniqueId = id->second;
        const platform::PinMetadata metadata{pinStore_->title(uniqueId),
                                             pinStore_->shortcutPath(uniqueId)};
        const std::shared_ptr<platform::PinSupport> pins = pins_;
        pixmap = icons_->iconFor(key, kIconSize, 1.0, [pins, uniqueId, metadata](int size) {
          return yasb::widgets::loadPinnedIcon(*pins, uniqueId, metadata, size);
        });
      }
    } else {
      pixmap = icons_->icon(key, kIconSize, 1.0);
    }
  } catch (const std::exception& error) {
    qDebug().noquote() << "Taskbar: icon of" << key << "failed:" << error.what();
  }
  if (!pixmap.isNull()) {
    entry.image = pixmap.toImage();
    ++entry.revision;
  }
  return entry.image;
}

int TaskbarService::iconRevision(platform::WindowHandle key) const {
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
  if (any) emit changed();
}

platform::WindowHandle TaskbarService::pseudoFor(const QString& uniqueId) {
  const auto found = pseudoByUniqueId_.find(uniqueId);
  if (found != pseudoByUniqueId_.end()) return found->second;
  const platform::WindowHandle pseudo = kPseudoBase | (pseudoByUniqueId_.size() + 1);
  pseudoByUniqueId_[uniqueId] = pseudo;
  uniqueIdByPseudo_[pseudo] = uniqueId;
  return pseudo;
}

// --- actions

void TaskbarService::activate(platform::WindowHandle hwnd) {
  if (isPseudo(hwnd)) return;
  bringToForeground(hwnd);
}

// Port of TaskbarWidget::bringToForeground without the preview and flash timers.
void TaskbarService::bringToForeground(platform::WindowHandle hwnd) {
  guarded("activate", [&] {
    if (!system_->isWindow(hwnd)) return;
    const auto [base, focusTarget] = system_->resolveBaseAndFocus(hwnd);
    const TaskbarWindow* tracked = window(base);
    const bool isActive = (tracked && tracked->info.isActive) || system_->isOwnerRootActive(base);

    if (system_->isIconic(base)) {
      system_->restoreWindow(base);
      system_->setForeground(base);
      const platform::WindowHandle target = focusTarget ? focusTarget : base;
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

void TaskbarService::cycle(const std::vector<platform::WindowHandle>& members) {
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

void TaskbarService::closeWindow(platform::WindowHandle hwnd) {
  if (isPseudo(hwnd)) return;
  guarded("close", [&] {
    if (system_->isWindow(hwnd)) system_->closeApplication(hwnd, false);
  });
}

void TaskbarService::launchPinned(const QString& uniqueId) {
  guarded("launch", [&] {
    const platform::PinMetadata metadata{pinStore_->title(uniqueId),
                                         pinStore_->shortcutPath(uniqueId)};
    pins_->launch(uniqueId, metadata, QString());
  });
}

void TaskbarService::pinWindow(platform::WindowHandle hwnd) {
  guarded("pin", [&] {
    const TaskbarWindow* w = window(hwnd);
    if (!w || !w->uniqueId) {
      qWarning() << "Taskbar: cannot pin, the app could not be identified";
      return;
    }
    platform::PinMetadata metadata = pins_->resolveMetadata(w->info, *w->uniqueId);
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
    : QQuickImageProvider(QQuickImageProvider::Image), service_(service) {}

QImage WindowIconProvider::requestImage(const QString& id, QSize* size,
                                        const QSize& /*requestedSize*/) {
  if (size) *size = QSize(0, 0);
  if (!service_) return {};
  bool ok = false;
  const platform::WindowHandle key = id.section(QLatin1Char('/'), 0, 0).toULongLong(&ok);
  if (!ok) return {};
  const QImage image = service_->iconImage(key);
  if (image.isNull()) return {};
  if (size) *size = image.size();
  return image;
}

}  // namespace tidy::services
