#include "widgets/taskbar/taskbar_widget.h"

#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QDrag>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStyle>
#include <algorithm>
#include <exception>

#include "core/bar/theme_class.h"
#include "core/widgets/backdrop.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/tooltip.h"
#include "widgets/taskbar/pin_store.h"
#include "widgets/taskbar/recycle_bin_monitor.h"
#include "widgets/taskbar/taskbar_drop.h"
#include "widgets/taskbar/taskbar_icons.h"
#include "widgets/taskbar/taskbar_preview.h"

namespace yasb::widgets {
namespace {

constexpr int kMaxWidgetWidth = 16777215;

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

QString classOf(const QWidget* widget) { return widget->property("class").toString(); }

// Python len()/slicing/ljust work on code points; QString counts UTF-16 units.
qsizetype codePoints(const QString& text) { return text.toUcs4().size(); }

QString leftCodePoints(const QString& text, qsizetype count) {
  const QList<uint> points = text.toUcs4();
  return QString::fromUcs4(reinterpret_cast<const char32_t*>(points.constData()),
                           std::min<qsizetype>(count, points.size()));
}

}  // namespace

platform::WindowHandle pseudoHwndFor(const QString& uniqueId) {
  // Python: -(abs(hash(unique_id)) % 1000000000 + 1000000000), a negative number no window uses.
  const std::int64_t magnitude =
      static_cast<std::int64_t>(qHash(uniqueId) % 1000000000) + 1000000000;
  return static_cast<platform::WindowHandle>(-magnitude);
}

// --- AppButton

AppButton::AppButton(TaskbarWidget* taskbar, platform::WindowHandle hwnd)
    : taskbar_(taskbar), hwnd_(hwnd) {
  setAcceptDrops(false);
  setObjectName(QStringLiteral("yasb-taskbar-btn-%1").arg(hwnd));

  previewTimer_.setSingleShot(true);
  previewTimer_.setInterval(taskbar->previewDelay());
  connect(&previewTimer_, &QTimer::timeout, this, [this] { onPreviewTimeout(); });
}

void AppButton::setHwnd(platform::WindowHandle hwnd) { hwnd_ = hwnd; }

void AppButton::enterEvent(QEnterEvent* event) {
  guarded("button enter", [&] {
    if (taskbar_->previewEnabled() && !dragging_) {
      PreviewManager* manager = taskbar_->previewManager();
      PreviewPopup* popup = manager ? manager->popup() : nullptr;
      if (popup && popup->isVisible() && popup->srcHwnd() == hwnd_) {
        // A preview for this window is already up, keep it instead of starting a new one.
        popup->cancelHide();
        return;
      }
      previewTimer_.start();
    }
  });
  QFrame::enterEvent(event);
}

void AppButton::leaveEvent(QEvent* event) {
  guarded("button leave", [&] {
    previewTimer_.stop();
    // A longer delay lets the pointer reach the preview.
    if (taskbar_->previewEnabled()) QTimer::singleShot(200, this, [this] { checkHidePreview(); });
  });
  QFrame::leaveEvent(event);
}

void AppButton::checkHidePreview() {
  guarded("hide preview check", [&] {
    const QPoint cursor = QCursor::pos();
    // If the pointer moved onto another button, that button's enter manages the preview.
    for (QWidget* w = QApplication::widgetAt(cursor); w; w = w->parentWidget()) {
      if (qobject_cast<AppButton*>(w)) return;
    }
    PreviewManager* manager = taskbar_->previewManager();
    PreviewPopup* popup = manager ? manager->popup() : nullptr;
    if (popup && popup->isVisible() && popup->keepsHover(cursor)) {
      QTimer::singleShot(200, this, [this] { checkHidePreview(); });
      return;
    }
    taskbar_->hidePreview();
  });
}

void AppButton::onPreviewTimeout() {
  guarded("preview timer", [&] {
    if (taskbar_->previewEnabled() && !dragging_) taskbar_->showPreviewForHwnd(hwnd_, this);
  });
}

void AppButton::restartPreviewIfHovered() {
  // After a drag, a pointer still over the button previews again.
  guarded("preview restart", [&] {
    if (rect().contains(mapFromGlobal(QCursor::pos())) && taskbar_->previewEnabled())
      previewTimer_.start();
  });
}

// Starts a QDrag once the pointer moved past the drag threshold; the drop target reorders.
bool AppButton::maybeStartDrag(QMouseEvent* event) {
  const int threshold = std::max(8, QApplication::startDragDistance());
  const int moved = (event->globalPosition().toPoint() - pressGlobalPos_).manhattanLength();
  if (moved < threshold || !leftPressed_) return false;
  dragging_ = true;
  guarded("drag start", [&] { taskbar_->setDragging(true); });
  auto* drag = new QDrag(this);
  auto* mime = new QMimeData;
  mime->setText(QString::number(static_cast<qulonglong>(hwnd_)));
  drag->setMimeData(mime);
  QPointer<AppButton> self(this);
  drag->exec(Qt::MoveAction);
  // The button can be deleted while the drag runs (its window closed).
  if (!self) return true;
  // Always reset the drag state.
  dragging_ = false;
  guarded("drag end", [&] { taskbar_->setDragging(false); });
  restartPreviewIfHovered();
  return true;
}

void AppButton::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    pressGlobalPos_ = event->globalPosition().toPoint();
    dragging_ = false;
    leftPressed_ = true;
    event->accept();
    return;
  }
  // The other buttons fall through to the widget's callbacks (context menu, middle click).
  QFrame::mousePressEvent(event);
}

void AppButton::mouseMoveEvent(QMouseEvent* event) {
  if ((event->buttons() & Qt::LeftButton) && !dragging_) maybeStartDrag(event);
  event->accept();
}

void AppButton::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const bool wasDragging = dragging_;
    dragging_ = false;
    leftPressed_ = false;
    guarded("drag end", [&] { taskbar_->setDragging(false); });
    if (!wasDragging) {
      if (rect().contains(event->position().toPoint())) {
        guarded("button click", [&] { taskbar_->onButtonClicked(hwnd_, this); });
      }
    } else {
      restartPreviewIfHovered();
    }
    event->accept();
    return;
  }
  QFrame::mouseReleaseEvent(event);
}

// --- TaskbarWidget

TaskbarWidget::TaskbarWidget(const WidgetContext& context, TaskbarDeps deps)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 QStringLiteral("taskbar-widget"), 0),
      options_(std::get<config::options::TaskbarConfig>(context.typedOptions)),
      globalState_(context.globalState),
      system_(std::move(deps.system)),
      tracker_(deps.tracker),
      animationEnabled_(options_.animation.enabled),
      animationDuration_(options_.animation.duration),
      groupingEnabled_(options_.grouping.enabled),
      previewEnabled_(options_.preview.enabled),
      previewDelay_(options_.preview.delay),
      tooltip_(options_.tooltip && !options_.preview.enabled),
      pins_(deps.pins ? std::move(deps.pins) : platform::makeWin32PinSupport()),
      tooltipEnabled_(options_.tooltip) {
  icons_ = deps.icons ? deps.icons : new TaskbarIcons(system_);
  icons_->setParent(this);

  asyncRecycleBin_ = deps.asyncRecycleBin;
  // Pinned apps are global: every taskbar shares one store and hears its changes.
  pinStore_ =
      deps.pinStore
          ? deps.pinStore
          : new PinStore(deps.pinFile.isEmpty() ? PinStore::defaultFile() : deps.pinFile, this);
  connect(pinStore_, &PinStore::pinnedAppsChanged, this,
          [this](const QString& action, const QString& id) {
            guarded("pinned apps changed", [&] { onPinnedAppsChanged(action, id); });
          });

  // The drop target behind the buttons: reorders pinned apps, raises windows on a file drag hover.
  dropHandler_ = new TaskbarDropHandler(this, labelContainer());
  connect(dropHandler_, &TaskbarDropHandler::dragStarted, this, [this] { setDragging(true); });
  connect(dropHandler_, &TaskbarDropHandler::dragEnded, this, [this] { setDragging(false); });
  connect(icons_, &TaskbarIcons::iconsChanged, this,
          [this] { guarded("reload icons", [&] { reloadIcons(); }); });

  registerCallback(QStringLiteral("toggle_window"),
                   [this](const QStringList&) { onToggleWindow(); });
  registerCallback(QStringLiteral("close_app"), [this](const QStringList&) { onCloseApp(); });
  registerCallback(QStringLiteral("context_menu"), [this](const QStringList&) { onContextMenu(); });
  setCallbackBinding({QString::fromStdString(options_.callbacks.on_left),
                      QString::fromStdString(options_.callbacks.on_middle),
                      QString::fromStdString(options_.callbacks.on_right),
                      QStringLiteral("do_nothing")});

  if (QCoreApplication* app = QCoreApplication::instance())
    connect(app, &QCoreApplication::aboutToQuit, this, [this] { stopEvents(); });

  if (previewEnabled_) {
    auto thumbnails =
        deps.thumbnails
            ? deps.thumbnails
            : std::shared_ptr<platform::ThumbnailBackend>(platform::makeWin32ThumbnailBackend());
    auto livePreview =
        deps.livePreview
            ? deps.livePreview
            : std::shared_ptr<platform::LivePreviewBackend>(platform::makeWin32LivePreview());
    previewManager_ = new PreviewManager(
        this, system_, thumbnails, livePreview, options_.preview.width, options_.preview.padding,
        options_.preview.margin, options_.preview.blur, options_.preview.peek);
  }
}

TaskbarWidget::~TaskbarWidget() { stopEvents(); }

void TaskbarWidget::stopEvents() {
  for (auto& [hwnd, animation] : animatingWidgets_) {
    if (animation) animation->stop();
  }
  animatingWidgets_.clear();
  const std::vector<platform::WindowHandle> flashing = [&] {
    std::vector<platform::WindowHandle> keys;
    for (const auto& entry : flashTimers_) keys.push_back(entry.first);
    return keys;
  }();
  for (platform::WindowHandle hwnd : flashing) stopFlashTimer(hwnd);
  if (previewManager_) previewManager_->stop();
}

void TaskbarWidget::setClassProperty(QWidget* widget, const QString& cssClass) {
  widget->setProperty("class", cssClass);
}

void TaskbarWidget::refreshStyle(std::initializer_list<QWidget*> widgets) {
  for (QWidget* widget : widgets) {
    if (!widget) continue;
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
  }
}

int TaskbarWidget::buttonCount() const {
  return labelContainer()->layout() ? labelContainer()->layout()->count() : 0;
}

void TaskbarWidget::insertButton(int position, AppButton* button) {
  auto* layout = qobject_cast<QBoxLayout*>(labelContainer()->layout());
  if (!layout) return;
  button->setParent(labelContainer());
  layout->insertWidget(position, button);
  button->show();
}

void TaskbarWidget::removeButton(AppButton* button) {
  if (button && labelContainer()->layout()) labelContainer()->layout()->removeWidget(button);
}

// --- tracker wiring

void TaskbarWidget::showEvent(QShowEvent* event) {
  guarded("show", [&] { BaseWidget::showEvent(event); });

  // Cache the DPI when the widget is first shown and attached to a screen.
  if (!dpi_) {
    const QScreen* screen = this->screen();
    dpi_ = screen ? screen->devicePixelRatio() : 1.0;
    guarded("reload icons", [&] { reloadIcons(); });
  }

  // Load and show the pinned apps first, so running windows are inserted at their pinned spot.
  guarded("pinned apps", [&] { displayPinnedApps(); });

  guarded("connect tracker", [&] {
    if (tracker_ && !trackerConnected_) {
      // start() delivers the already open windows synchronously.
      initialPopulation_ = true;
      connectTracker();
      trackerConnected_ = true;
      initialPopulation_ = false;
    }
  });

  if (hwndToWidget_.empty() && pinStore_->isEmpty() && options_.hide_empty) {
    QTimer::singleShot(0, this, [this] { hideTaskbarWidget(); });
  }
}

void TaskbarWidget::connectTracker() {
  using platform::WindowTracker;
  connect(tracker_, &WindowTracker::windowAdded, this, &TaskbarWidget::onWindowAdded);
  connect(tracker_, &WindowTracker::windowRemoved, this, &TaskbarWidget::onWindowRemoved);
  connect(tracker_, &WindowTracker::windowUpdated, this, &TaskbarWidget::onWindowUpdated);
  connect(tracker_, &WindowTracker::windowMonitorChanged, this,
          &TaskbarWidget::onWindowMonitorChanged);
  connect(tracker_, &WindowTracker::windowMinimizeChanged, this,
          &TaskbarWidget::onWindowMinimizeChanged);

  // Keep cloaked tasks if any connected widget has show_only_visible = false; each widget filters
  // independently in shouldShowWindow.
  tracker_->keepCloakedTasks(!options_.show_only_visible);

  QWidget* top = window();
  if (!tracker_->isInitialized()) {
    if (!top || !tracker_->start(static_cast<platform::WindowHandle>(top->winId())))
      qWarning() << "Taskbar could not start the window tracker; it will stay empty";
  }
  // Send the windows that were already open to this widget (the first one also got them from
  // start()); addWindowUi ignores a window it already shows.
  for (const auto& [hwnd, info] : tracker_->windows()) {
    guarded("existing window", [&] { onWindowAdded(hwnd, info); });
  }
}

// --- tracker signals

void TaskbarWidget::onWindowAdded(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  if (!shouldShowWindow(hwnd, info)) return;
  addWindowUi(hwnd, info);
}

void TaskbarWidget::onWindowRemoved(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  // Skip if we do not show this window, to avoid duplicate removals.
  if (windowButtons_.count(hwnd)) removeWindowUi(hwnd, info);
}

void TaskbarWidget::onWindowUpdated(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  // Apply filtering: remove the button when the window no longer qualifies.
  if (!shouldShowWindow(hwnd, info)) {
    if (windowButtons_.count(hwnd)) removeWindowUi(hwnd, info, /*immediate=*/true);
    return;
  }
  if (!windowButtons_.count(hwnd))
    addWindowUi(hwnd, info);
  else
    updateWindowUi(hwnd, info);
}

void TaskbarWidget::onWindowMinimizeChanged(platform::WindowHandle hwnd, bool minimized) {
  if (minimized)
    minimizedHwnds_.insert(hwnd);
  else
    minimizedHwnds_.erase(hwnd);

  AppButton* container = buttonFor(hwnd);
  if (!container) return;

  // Minimizing and restoring change whether the window counts as focused, so the focused-only
  // title has to be re-decided here too. It runs before the class early-outs below.
  if (!contextMenuOpen_) refreshTitleVisibility(hwnd);

  const QString newClass = containerClass(hwnd);
  if (classOf(container) == newClass ||
      (newClass.contains(QLatin1String("flashing")) && flashOwns(container)))
    return;
  setClassProperty(container, newClass);
  refreshStyle({container});
}

void TaskbarWidget::onWindowMonitorChanged(platform::WindowHandle hwnd,
                                           const platform::WindowInfo& info) {
  if (options_.monitor_exclusive) {
    if (shouldShowWindow(hwnd, info)) {
      // Moved onto our monitor: add it if it is not here yet.
      if (!windowButtons_.count(hwnd)) addWindowUi(hwnd, info);
    } else if (windowButtons_.count(hwnd)) {
      // Moved away from our monitor.
      removeWindowUi(hwnd, info, /*immediate=*/true);
    }
  } else if (!windowButtons_.count(hwnd) && shouldShowWindow(hwnd, info)) {
    addWindowUi(hwnd, info);
  } else {
    updateWindowUi(hwnd, info);
  }
}

bool TaskbarWidget::shouldShowWindow(platform::WindowHandle hwnd,
                                     const platform::WindowInfo& info) const {
  if (info.title.trimmed().isEmpty()) return false;
  if (!info.processName) return false;

  const auto contains = [](const std::vector<std::string>& list, const QString& value) {
    return std::find(list.begin(), list.end(), value.toStdString()) != list.end();
  };
  if (contains(options_.ignore_apps.titles, info.title)) return false;
  if (contains(options_.ignore_apps.classes, info.className)) return false;
  if (contains(options_.ignore_apps.processes, *info.processName)) return false;

  if (options_.strict_filtering && !info.canMinimize) return false;
  if (options_.show_only_visible && info.isCloaked) return false;

  if (options_.monitor_exclusive) {
    // An unknown (transient) monitor keeps existing buttons but does not add new ones.
    if (info.monitor == 0) return windowButtons_.count(hwnd) != 0;
    const QWidget* top = window();
    const platform::WindowHandle widgetMonitor =
        top ? system_->monitorOfWindow(static_cast<platform::WindowHandle>(top->winId())) : 0;
    if (top && info.monitor != widgetMonitor) return false;
  }
  return true;
}

// --- window UI

QPixmap TaskbarWidget::appIcon(platform::WindowHandle hwnd) {
  if (options_.icon_size <= 0 || !dpi_) return {};
  try {
    // A window of the pinned Recycle Bin shows the bin's state, not the window's icon.
    const auto pinned = runningPinned_.find(hwnd);
    if (pinned != runningPinned_.end() && platform::isRecycleBinId(pinned->second))
      return recycleBinIcon(recycleBinEmpty_);
    return icons_->icon(hwnd, options_.icon_size, *dpi_);
  } catch (const std::exception& error) {
    qDebug().noquote() << "Failed to get the icon of window" << hwnd << error.what();
    return {};
  }
}

void TaskbarWidget::addWindowUi(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  if (suspendUpdates_) return;
  // Skip a window that is already shown (prevents duplicate calls).
  if (windowButtons_.count(hwnd)) return;
  // Apps already minimized when the button appears; minimize events keep it current after.
  if (system_->isIconic(hwnd)) minimizedHwnds_.insert(hwnd);

  const std::optional<QString> uniqueId = system_->appUniqueId(info);
  const QString title = info.title;
  const QString process = info.processName.value_or(QString());
  // Pinned apps are global across monitors.
  const bool isPinned = uniqueId && pinStore_->contains(*uniqueId);

  // Another window of the same app is already on the bar: attach it to that button.
  QString groupKey;
  if (groupingEnabled_ && uniqueId) groupKey = *uniqueId;
  if (!groupKey.isEmpty()) {
    const auto group = groupHwnds_.find(groupKey);
    if (group != groupHwnds_.end() && !group->second.empty()) {
      if (AppButton* container = buttonFor(group->second.front())) {
        if (isPinned) runningPinned_[hwnd] = groupKey;
        group->second.push_back(hwnd);
        hwndToGroup_[hwnd] = groupKey;
        hwndToWidget_[hwnd] = container;
        windowButtons_[hwnd] = WindowEntry{title, appIcon(hwnd), hwnd, process, info};
        syncGroupButton(groupKey);
        return;
      }
      groupHwnds_.erase(group);
    }
  }

  // A pinned app that starts takes the place of its pinned-only button.
  int insertPosition = -1;
  if (isPinned) {
    bool foundPinnedButton = false;
    for (const auto& [pseudo, widget] :
         std::map<platform::WindowHandle, QPointer<AppButton>>(hwndToWidget_)) {
      if (!widget || !isPseudoHwnd(pseudo) || widget->property("unique_id").toString() != *uniqueId)
        continue;
      insertPosition = hwndPosition(pseudo);
      if (animationEnabled_) {
        if (const auto running = animatingWidgets_.find(pseudo);
            running != animatingWidgets_.end()) {
          if (running->second) running->second->stop();
          animatingWidgets_.erase(running);
        }
        animateContainer(widget, widget->width(), 0, animationDuration_, std::nullopt, true);
      } else {
        removeButton(widget);
        widget->deleteLater();
      }
      hwndToWidget_.erase(pseudo);
      foundPinnedButton = true;
      break;
    }
    // No pinned-only button: the app already runs, so go after its other windows.
    if (!foundPinnedButton) {
      int lastPosition = -1;
      for (const auto& [existing, widget] :
           std::map<platform::WindowHandle, QPointer<AppButton>>(hwndToWidget_)) {
        if (!widget) continue;
        const auto running = runningPinned_.find(existing);
        if (running != runningPinned_.end() && running->second == *uniqueId)
          lastPosition = hwndPosition(existing);
      }
      if (lastPosition >= 0) {
        insertPosition = lastPosition + 1;
      } else if (const int desired = static_cast<int>(pinStore_->order().indexOf(*uniqueId));
                 desired >= 0 && desired <= buttonCount()) {
        // Out of range stays -1 and adds at the end.
        insertPosition = desired;
      }
    }
    runningPinned_[hwnd] = *uniqueId;
  }

  // Clean up any existing widget or animation for this window.
  if (const auto running = animatingWidgets_.find(hwnd); running != animatingWidgets_.end()) {
    if (running->second) running->second->stop();
    animatingWidgets_.erase(running);
  }
  if (const auto old = hwndToWidget_.find(hwnd); old != hwndToWidget_.end()) {
    if (old->second) {
      removeButton(old->second);
      old->second->deleteLater();
    }
    hwndToWidget_.erase(old);
  }
  windowButtons_.erase(hwnd);

  // Create the window's button.
  const QPixmap icon = appIcon(hwnd);
  windowButtons_[hwnd] = WindowEntry{title, icon, hwnd, process, info};

  AppButton* container = createAppContainer(title, icon, hwnd);
  hwndToWidget_[hwnd] = container;
  if (!groupKey.isEmpty()) {
    groupHwnds_[groupKey] = {hwnd};
    hwndToGroup_[hwnd] = groupKey;
  }

  if (isPinned) {
    container->setProperty("pinned", true);
    container->setProperty("unique_id", *uniqueId);
    setClassProperty(container, classOf(container) + QStringLiteral(" pinned"));
  }

  // Pinned apps replace their button, unpinned apps go at the end. Animating the whole startup
  // burst at once is what stutters, so skip it for the initial population and only animate
  // windows added after that.
  const int position = insertPosition >= 0 ? insertPosition : buttonCount();
  if (animationEnabled_ && !initialPopulation_) {
    container->setFixedWidth(0);
    insertButton(position, container);
    animateContainer(container, 0, container->sizeHint().width(), animationDuration_, hwnd);
  } else {
    insertButton(position, container);
  }

  if (options_.hide_empty && !hwndToWidget_.empty()) showTaskbarWidget();
}

void TaskbarWidget::removeWindowUi(platform::WindowHandle hwnd,
                                   const std::optional<platform::WindowInfo>& info,
                                   bool immediate) {
  if (suspendUpdates_) return;
  icons_->forget(hwnd);

  // Grouped button: drop only this window while other windows of the app stay open.
  if (const auto grouped = hwndToGroup_.find(hwnd); grouped != hwndToGroup_.end()) {
    const QString groupKey = grouped->second;
    hwndToGroup_.erase(grouped);
    auto& members = groupHwnds_[groupKey];
    members.erase(std::remove(members.begin(), members.end(), hwnd), members.end());
    if (!members.empty()) {
      animatingWidgets_.erase(hwnd);
      runningPinned_.erase(hwnd);
      hwndToWidget_.erase(hwnd);
      windowButtons_.erase(hwnd);
      syncGroupButton(groupKey);
      // The button lives on, so re-decide its blink from the windows it still holds instead of
      // ending it because one of them closed.
      if (AppButton* container = buttonFor(groupHwnds_[groupKey].front())) {
        updateFlashState(container->hwnd(), container);
        refreshTitleVisibility(container->hwnd());
      }
      return;
    }
    groupHwnds_.erase(groupKey);
  }

  minimizedHwnds_.erase(hwnd);

  // Cancel any running animation.
  if (const auto running = animatingWidgets_.find(hwnd); running != animatingWidgets_.end()) {
    if (running->second) running->second->stop();
    animatingWidgets_.erase(running);
    immediate = true;
  }

  stopFlashTimer(hwnd);

  // A pinned app that closes gets its pinned-only button back.
  QString pinnedId;
  if (const auto running = runningPinned_.find(hwnd); running != runningPinned_.end()) {
    pinnedId = running->second;
    runningPinned_.erase(running);
  }
  const bool isPinned = !pinnedId.isEmpty() && pinStore_->contains(pinnedId);

  // Some apps (the Nvidia App) send remove events with no process info when they close.
  if (info && !info->processName && !isPinned) {
    const auto found = hwndToWidget_.find(hwnd);
    if (found != hwndToWidget_.end()) {
      if (found->second) {
        removeButton(found->second);
        found->second->deleteLater();
      }
      hwndToWidget_.erase(found);
    }
    windowButtons_.erase(hwnd);
    return;
  }

  // The spot the pinned button goes back to is the one the window leaves.
  const int currentPosition = isPinned ? hwndPosition(hwnd) : -1;

  QPointer<AppButton> widget;
  if (const auto found = hwndToWidget_.find(hwnd); found != hwndToWidget_.end()) {
    widget = found->second;
    hwndToWidget_.erase(found);
  }
  if (widget) {
    if (animationEnabled_ && !immediate) {
      animateContainer(widget, widget->width(), 0, animationDuration_, std::nullopt, true);
    } else {
      removeButton(widget);
      widget->deleteLater();
    }
  }
  windowButtons_.erase(hwnd);

  // Only recreate the pinned button when no other window of the app still runs.
  if (isPinned) {
    const bool otherWindows =
        std::any_of(runningPinned_.begin(), runningPinned_.end(),
                    [&](const auto& entry) { return entry.second == pinnedId; });
    if (!otherWindows && !pendingPinnedRecreations_.count(pinnedId) && currentPosition >= 0) {
      // Pending marks the scheduled recreation so it is not queued twice.
      pendingPinnedRecreations_.insert(pinnedId);
      QTimer::singleShot(0, this, [this, pinnedId, currentPosition] {
        guarded("recreate pinned button", [&] { recreatePinnedButton(pinnedId, currentPosition); });
      });
    }
  }

  // Hide only with no buttons left and no recreation pending.
  if (options_.hide_empty && hwndToWidget_.empty() && pendingPinnedRecreations_.empty())
    hideTaskbarWidget();
}

void TaskbarWidget::updateWindowUi(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  if (suspendUpdates_) return;
  const QString title = info.title;
  const QPixmap icon = appIcon(hwnd);
  windowButtons_[hwnd] = WindowEntry{title, icon, hwnd, info.processName.value_or(QString()), info};

  AppButton* widget = buttonFor(hwnd);
  if (!widget) return;

  // Grouped button: icon, title and state are rendered from the group, not from this window.
  if (const auto group = hwndToGroup_.find(hwnd); group != hwndToGroup_.end()) {
    guarded("grouped update", [&] {
      if (info.isActive) clearOthersSetForeground(hwnd);
      syncGroupButton(group->second);
      if (!contextMenuOpen_) refreshTitleVisibility(widget->hwnd());
      updateFlashState(widget->hwnd(), widget);
    });
    return;
  }

  if (!icon.isNull() && widget->iconLabel) widget->iconLabel->setPixmap(icon);

  guarded("title update", [&] {
    if (options_.title_label.enabled && widget->titleLabel) {
      const QString formatted = formatTitle(title);
      if (widget->titleLabel->text() != formatted) widget->titleLabel->setText(formatted);
      if (options_.title_label.show == "focused" && widget->titleWrapper && !contextMenuOpen_) {
        const bool desired = titleVisibility(hwnd);
        QVariant target = widget->titleWrapper->property("target_visible");
        const bool current = target.isValid() ? target.toBool() : widget->titleWrapper->isVisible();
        if (desired != current) {
          animateOrSetTitleVisible(widget, desired);
          widget->layout()->activate();
        }
      }
    }
  });

  // If the tracker marks this window active, enforce a single foreground class. This handles
  // external changes (minimizing through the Windows taskbar) where only the newly active window
  // might get an update at first.
  guarded("foreground", [&] {
    if (info.isActive) clearOthersSetForeground(hwnd);
  });

  guarded("repolish", [&] {
    const QString newClass = containerClass(hwnd);
    if (!(newClass.contains(QLatin1String("flashing")) && flashOwns(widget)))
      setClassProperty(widget, newClass);
    refreshStyle({widget, widget->titleWrapper, widget->titleLabel});
    updateFlashState(hwnd, widget);
  });

  if (tooltip_ && !title.isEmpty()) setTooltip(widget, title, globalState_, 0);
}

void TaskbarWidget::syncGroupButton(const QString& groupKey) {
  const auto group = groupHwnds_.find(groupKey);
  if (group == groupHwnds_.end() || group->second.empty()) return;
  const std::vector<platform::WindowHandle> members = group->second;
  AppButton* container = buttonFor(members.front());
  if (!container) return;

  // The focused window represents the group for icon, title, clicks and context menu, and the last
  // focused one keeps representing it once focus moves to another app.
  platform::WindowHandle hwnd = 0;
  bool found = false;
  for (platform::WindowHandle member : members) {
    if (tracker_ && tracker_->isActive(member)) {
      hwnd = member;
      found = true;
      break;
    }
  }
  if (!found) {
    hwnd = std::find(members.begin(), members.end(), container->hwnd()) != members.end()
               ? container->hwnd()
               : members.front();
  }

  container->setHwnd(hwnd);
  QFrame* titleWrapper = options_.title_label.enabled ? container->titleWrapper : nullptr;
  QLabel* titleLabel = titleWrapper ? container->titleLabel : nullptr;
  for (QWidget* child : std::initializer_list<QWidget*>{
           container, container->iconLabel, titleWrapper, titleLabel, container->countLabel}) {
    if (child) child->setProperty("hwnd", static_cast<qulonglong>(hwnd));
  }

  QString title;
  QPixmap icon;
  windowEntry(hwnd, title, icon);
  if (container->iconLabel && !icon.isNull()) container->iconLabel->setPixmap(icon);
  if (titleLabel) titleLabel->setText(formatTitle(title));
  if (tooltip_ && !title.isEmpty()) setTooltip(container, title, globalState_, 0);

  const QString groupClass = containerClass(hwnd);
  const bool wasGrouped = classOf(container).contains(QLatin1String("grouped"));
  if (!(groupClass.contains(QLatin1String("flashing")) && flashOwns(container)))
    setClassProperty(container, groupClass);
  refreshStyle({container, container->countLabel, titleWrapper, titleLabel});

  // Qt resolves padding and border width once and keeps them, so a button that becomes grouped
  // later never picks up the grouped rule's box model. Polishing does not redo it, assigning a
  // style sheet is the only thing that does.
  if (wasGrouped != groupClass.contains(QLatin1String("grouped")))
    container->setStyleSheet(container->styleSheet());

  if (container->countLabel) {
    container->countLabel->setText(QString::number(members.size()));
    container->countLabel->setVisible(members.size() > 1);
  }
}

AppButton* TaskbarWidget::createAppContainer(const QString& title, const QPixmap& icon,
                                             platform::WindowHandle hwnd) {
  auto* container = new AppButton(this, hwnd);
  container->setProperty("class", containerClass(hwnd));
  container->setProperty("hwnd", static_cast<qulonglong>(hwnd));

  // Outer layout without margins; the content wrapper holds the icon, title and count.
  auto* outer = new QHBoxLayout(container);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);

  auto* wrapper = new QFrame;
  auto* content = new QHBoxLayout(wrapper);
  content->setContentsMargins(0, 0, 0, 0);
  content->setSpacing(0);

  if (options_.icon_size > 0) {
    auto* iconLabel = new QLabel;
    iconLabel->setProperty("class", "app-icon");
    iconLabel->setFixedSize(options_.icon_size, options_.icon_size);
    if (!icon.isNull()) iconLabel->setPixmap(icon);
    iconLabel->setProperty("hwnd", static_cast<qulonglong>(hwnd));
    content->addWidget(iconLabel);
    container->iconLabel = iconLabel;
  }

  if (options_.title_label.enabled) {
    // The label sits in a wrapper so only the wrapper's width animates, which reduces reflow.
    auto* titleWrapper = new QFrame;
    titleWrapper->setProperty("hwnd", static_cast<qulonglong>(hwnd));
    auto* wrapperLayout = new QHBoxLayout(titleWrapper);
    wrapperLayout->setContentsMargins(0, 0, 0, 0);
    wrapperLayout->setSpacing(0);
    titleWrapper->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    auto* titleLabel = new QLabel(formatTitle(title));
    titleLabel->setProperty("class", "app-title");
    titleLabel->setProperty("hwnd", static_cast<qulonglong>(hwnd));
    titleLabel->setSizePolicy(QSizePolicy::Preferred, titleLabel->sizePolicy().verticalPolicy());
    wrapperLayout->addWidget(titleLabel);
    content->addWidget(titleWrapper);
    container->titleWrapper = titleWrapper;
    container->titleLabel = titleLabel;

    if (options_.title_label.show == "focused") {
      const bool initialVisible = titleVisibility(hwnd);
      titleWrapper->setProperty("target_visible", initialVisible);
      titleWrapper->setVisible(initialVisible);
      if (!initialVisible) titleWrapper->setMaximumWidth(0);
    }
  }

  // Window counter, only visible once the button holds more than one window.
  if (groupingEnabled_ && options_.grouping.show_count) {
    auto* countLabel = new QLabel(QString());
    countLabel->setProperty("class", "app-count");
    countLabel->setAlignment(Qt::AlignCenter);
    countLabel->setProperty("hwnd", static_cast<qulonglong>(hwnd));
    countLabel->setVisible(false);
    content->addWidget(countLabel);
    container->countLabel = countLabel;
  }

  outer->addWidget(wrapper);
  if (tooltip_) setTooltip(container, title, globalState_, 0);
  return container;
}

// --- state helpers

std::vector<platform::WindowHandle> TaskbarWidget::membersOf(platform::WindowHandle hwnd) const {
  if (const auto group = hwndToGroup_.find(hwnd); group != hwndToGroup_.end()) {
    if (const auto members = groupHwnds_.find(group->second);
        members != groupHwnds_.end() && !members->second.empty())
      return members->second;
  }
  return {hwnd};
}

AppButton* TaskbarWidget::buttonFor(platform::WindowHandle hwnd) const {
  const auto found = hwndToWidget_.find(hwnd);
  return found != hwndToWidget_.end() ? found->second.data() : nullptr;
}

std::vector<AppButton*> TaskbarWidget::buttons() const {
  std::vector<AppButton*> result;
  if (const QLayout* layout = labelContainer()->layout()) {
    for (int i = 0; i < layout->count(); ++i) {
      if (auto* button = qobject_cast<AppButton*>(layout->itemAt(i)->widget()))
        result.push_back(button);
    }
  }
  return result;
}

std::vector<platform::WindowHandle> TaskbarWidget::groupMembers(const QString& key) const {
  const auto found = groupHwnds_.find(key);
  return found != groupHwnds_.end() ? found->second : std::vector<platform::WindowHandle>{};
}

bool TaskbarWidget::animating(platform::WindowHandle hwnd) const {
  const auto found = animatingWidgets_.find(hwnd);
  return found != animatingWidgets_.end() && found->second;
}

bool TaskbarWidget::windowEntry(platform::WindowHandle hwnd, QString& title, QPixmap& icon) const {
  const auto found = windowButtons_.find(hwnd);
  if (found == windowButtons_.end()) return false;
  title = found->second.title;
  icon = found->second.icon;
  return true;
}

bool TaskbarWidget::isWindowFlashing(platform::WindowHandle hwnd) const {
  return tracker_ && tracker_->isFlashing(hwnd);
}

// True once no window of a button is left on screen. IsIconic is the authority, not the cached set:
// a tiling manager keeps focus on a window it just minimized, so activation events name windows
// that are still iconic and would clear their cached flag. Reading the window itself at render
// time also sidesteps the reverse race: on a restore the activation arrives while IsIconic is
// still 1 and only flips a moment later. The cached set stays as a fallback.
bool TaskbarWidget::isMinimized(const std::vector<platform::WindowHandle>& members) const {
  if (members.empty()) return false;
  for (platform::WindowHandle member : members) {
    try {
      if (!system_->isIconic(member)) return false;
    } catch (...) {
      if (!minimizedHwnds_.count(member)) return false;
    }
  }
  return true;
}

QString TaskbarWidget::containerClass(platform::WindowHandle hwnd) const {
  const std::vector<platform::WindowHandle> members = membersOf(hwnd);
  const QString base = members.size() > 1 ? QStringLiteral("app-container grouped")
                                          : QStringLiteral("app-container");

  const auto any = [&](auto&& test) { return std::any_of(members.begin(), members.end(), test); };
  if (any([&](platform::WindowHandle m) { return tracker_ && tracker_->isFlashing(m); }))
    return base + QStringLiteral(" flashing");
  if (isMinimized(members)) return base + QStringLiteral(" running minimized");

  const bool active =
      any([&](platform::WindowHandle m) { return tracker_ && tracker_->isActive(m); });
  const platform::WindowHandle foreground = system_->foregroundWindow();
  if (active || std::find(members.begin(), members.end(), foreground) != members.end())
    return base + QStringLiteral(" foreground");
  return base + QStringLiteral(" running");
}

QString TaskbarWidget::formatTitle(const QString& title) const {
  const qsizetype maxLength = options_.title_label.max_length;
  QString formatted = codePoints(title) > maxLength
                          ? leftCodePoints(title, maxLength) + QStringLiteral("..")
                          : title;
  const qsizetype minLength = options_.title_label.min_length;
  const qsizetype length = codePoints(formatted);
  if (length < minLength) formatted += QString(minLength - length, QLatin1Char(' '));
  return formatted;
}

// Should the title show when show == "focused"? Normalized to the window's base owner.
bool TaskbarWidget::titleVisibility(platform::WindowHandle hwnd) const {
  for (platform::WindowHandle member : membersOf(hwnd)) {
    platform::WindowHandle base = member;
    try {
      base = system_->resolveBaseAndFocus(member).first;
    } catch (...) {
    }
    // A minimized window is not the focused one, whatever the activation said. Tiling managers keep
    // focus on a window they just minimized, and is_active would hold the title open for it.
    try {
      if (system_->isIconic(base)) continue;
    } catch (...) {
    }
    if (tracker_ && tracker_->isActive(base)) return true;
    // Then rely on owner/root being active (handles wrappers and child windows).
    try {
      if (system_->isOwnerRootActive(base)) return true;
    } catch (...) {
    }
    // Final fallback: foreground equality on the base window.
    try {
      if (base == system_->foregroundWindow()) return true;
    } catch (...) {
    }
  }
  return false;
}

void TaskbarWidget::refreshTitleVisibility(platform::WindowHandle hwnd) {
  if (!(options_.title_label.enabled && options_.title_label.show == "focused")) return;
  AppButton* container = buttonFor(hwnd);
  if (!container || !container->titleWrapper) return;
  const bool desired = titleVisibility(hwnd);
  const QVariant target = container->titleWrapper->property("target_visible");
  const bool current = target.isValid() ? target.toBool() : container->titleWrapper->isVisible();
  if (desired == current) return;
  animateOrSetTitleVisible(container, desired);
  if (container->layout()) container->layout()->activate();
}

void TaskbarWidget::animateOrSetTitleVisible(AppButton* button, bool visible) {
  QFrame* label = button->titleWrapper;
  if (!label) return;
  label->setProperty("target_visible", visible);
  const auto activateParentLayout = [label] {
    if (QWidget* parent = label->parentWidget()) {
      if (parent->layout()) parent->layout()->activate();
    }
  };
  if (button->titleAnimation) {
    button->titleAnimation->stop();
    button->titleAnimation->deleteLater();
    button->titleAnimation = nullptr;
  }

  if (!animationEnabled_) {
    label->setVisible(visible);
    label->setMaximumWidth(visible ? kMaxWidgetWidth : 0);
    activateParentLayout();
    return;
  }

  const int startWidth =
      label->maximumWidth() != kMaxWidgetWidth ? label->maximumWidth() : label->width();
  const int endWidth = visible ? label->sizeHint().width() : 0;
  // The label has to be visible before animating in.
  if (visible && !label->isVisible()) label->setVisible(true);

  auto* animation = new QPropertyAnimation(label, "maximumWidth", label);
  animation->setStartValue(startWidth);
  animation->setEndValue(endWidth);
  animation->setDuration(animationDuration_);
  animation->setEasingCurve(QEasingCurve::InOutQuad);
  QPointer<AppButton> guard(button);
  connect(animation, &QPropertyAnimation::finished, label,
          [label, visible, guard, activateParentLayout] {
            if (!visible) {
              label->setVisible(false);
              label->setMaximumWidth(0);
            } else {
              // Clear the width constraint so natural resizing works later.
              label->setMaximumWidth(kMaxWidgetWidth);
            }
            if (guard) guard->titleAnimation = nullptr;
            activateParentLayout();
          });
  button->titleAnimation = animation;
  animation->start();
}

// Sets a single "foreground" button, keeps the flashing the tracker reports and toggles the
// focused-only titles.
void TaskbarWidget::clearOthersSetForeground(std::optional<platform::WindowHandle> target) {
  // A focused window is not minimized, so activation clears the flag like a restore does.
  if (target) minimizedHwnds_.erase(*target);

  guarded("clear others", [&] {
    for (AppButton* button : buttons()) {
      const platform::WindowHandle hwnd = button->property("hwnd").toULongLong();
      const std::vector<platform::WindowHandle> members = membersOf(hwnd);
      const QString base = members.size() > 1 ? QStringLiteral("app-container grouped")
                                              : QStringLiteral("app-container");

      QString newClass = isMinimized(members) ? base + QStringLiteral(" running minimized")
                                              : base + QStringLiteral(" running");
      // Preserve flashing if the tracker reports it.
      for (platform::WindowHandle member : members) {
        if (tracker_ && tracker_->isFlashing(member)) {
          newClass = base + QStringLiteral(" flashing");
          break;
        }
      }
      // The target gets "foreground" (the tracker usually clears flashing on activation), unless it
      // is minimized: a tiling manager hands focus to windows that are still iconic, and being off
      // screen outranks being the activation target.
      if (target && std::find(members.begin(), members.end(), *target) != members.end() &&
          !isMinimized(members))
        newClass = base + QStringLiteral(" foreground");

      if (classOf(button) != newClass &&
          !(newClass.contains(QLatin1String("flashing")) && flashOwns(button))) {
        setClassProperty(button, newClass);
        refreshStyle({button});
      }

      // The focused-only title goes through the same authority as every other call site, and is
      // re-decided even when the class did not move, since a window can stop being focused
      // without changing class.
      if (!contextMenuOpen_) refreshTitleVisibility(hwnd);
    }
  });
}

// --- flashing

// Start, continue or end this button's blink from the tracker's flashing state. Read the tracker
// rather than the button's class property: that property is what the flash timer toggles, so
// testing it here would end the blink on its own off beat.
void TaskbarWidget::updateFlashState(platform::WindowHandle hwnd, AppButton* button) {
  if (containerClass(hwnd).contains(QLatin1String("flashing"))) {
    startFlashTimer(hwnd, button);
  } else {
    // No window of this button is asking any more, so the episode is over and the next window that
    // asks gets a fresh blink.
    button->blinked = false;
    stopFlashTimer(hwnd);
  }
}

bool TaskbarWidget::flashOwns(const QWidget* widget) const {
  return std::any_of(flashTimers_.begin(), flashTimers_.end(),
                     [&](const auto& entry) { return entry.second->container == widget; });
}

// Toggle the flashing class every second, stop after 10 and keep the flashing class.
void TaskbarWidget::startFlashTimer(platform::WindowHandle hwnd, AppButton* button) {
  // A grouped button is one button shared by several windows, so it gets one timer. Keyed by
  // window, a second window of the group would start a rival timer one second out of phase.
  if (flashOwns(button)) return;
  // The blink plays once per attention episode. is_flashing stays set until the window is
  // activated, so without this every later window event would replay the cycle.
  if (button->blinked) return;

  auto* timer = new FlashTimer(this, button);
  timer->setInterval(1000);
  connect(timer, &QTimer::timeout, this, [this, timer, hwnd] {
    guarded("flash", [&] {
      // Follow the button, not the window that started the timer: that window can close while the
      // group and its blink live on.
      AppButton* w = timer->container;
      if (!w) {
        stopFlashTimer(hwnd);
        return;
      }
      ++timer->count;
      const std::vector<platform::WindowHandle> members = membersOf(hwnd);
      const QString base = members.size() > 1 ? QStringLiteral("app-container grouped")
                                              : QStringLiteral("app-container");
      if (timer->count >= 10) {
        setClassProperty(w, base + QStringLiteral(" flashing"));
        w->blinked = true;
        refreshStyle({w});
        stopFlashTimer(hwnd);
        return;
      }
      const QString current = classOf(w);
      const QString off = isMinimized(members) ? base + QStringLiteral(" running minimized")
                                               : base + QStringLiteral(" running");
      setClassProperty(w, current.contains(QLatin1String("flashing"))
                              ? off
                              : base + QStringLiteral(" flashing"));
      // The timer owns both the class and the repaint while it runs, otherwise the blink is only
      // visible when some unrelated window event happens to repolish the button.
      refreshStyle({w});
    });
  });
  flashTimers_[hwnd] = timer;
  timer->start();
}

// Stop the flash timer for a button, whichever of its windows registered it. A grouped button
// changes which window represents it as focus moves inside the group, so the key used when the
// timer started is often not the one passed here.
void TaskbarWidget::stopFlashTimer(platform::WindowHandle hwnd) {
  std::vector<platform::WindowHandle> keys{hwnd};
  if (AppButton* widget = buttonFor(hwnd)) {
    for (const auto& [key, timer] : flashTimers_) {
      if (timer->container == widget) keys.push_back(key);
    }
  }
  for (platform::WindowHandle key : keys) {
    const auto found = flashTimers_.find(key);
    if (found == flashTimers_.end()) continue;
    found->second->stop();
    found->second->deleteLater();
    flashTimers_.erase(found);
  }
}

// --- animation

void TaskbarWidget::animateContainer(AppButton* container, int startWidth, int endWidth,
                                     int duration, std::optional<platform::WindowHandle> hwnd,
                                     bool isRemoving) {
  QPointer<AppButton> guard(container);
  const auto onFinished = [this, guard, hwnd, isRemoving] {
    if (hwnd) animatingWidgets_.erase(*hwnd);
    if (isRemoving) {
      // Only remove when explicitly requested.
      if (guard) {
        removeButton(guard);
        guard->setParent(nullptr);
        guard->deleteLater();
      }
      if (options_.hide_empty && hwndToWidget_.empty() && pendingPinnedRecreations_.empty())
        hideTaskbarWidget();
    } else if (guard) {
      guard->setMaximumWidth(kMaxWidgetWidth);
      guard->adjustSize();
      guard->updateGeometry();
    }
  };

  if (!animationEnabled_) {
    onFinished();
    return;
  }

  auto* animation = new QPropertyAnimation(container, "maximumWidth", container);
  animation->setStartValue(startWidth);
  animation->setEndValue(endWidth);
  animation->setDuration(duration);
  animation->setEasingCurve(QEasingCurve::InOutQuad);
  if (hwnd && endWidth > startWidth) animatingWidgets_[*hwnd] = animation;
  connect(animation, &QPropertyAnimation::finished, this,
          [onFinished] { guarded("animation", onFinished); });
  animation->start();
}

// --- visibility

void TaskbarWidget::showTaskbarWidget() {
  if (isVisible()) return;
  show();
}

void TaskbarWidget::hideTaskbarWidget() {
  if (isHidden()) {
    hidePreview();
    return;
  }
  hide();
  hidePreview();
}

// --- icons

void TaskbarWidget::reloadIcons() {
  for (const auto& [hwnd, widget] : hwndToWidget_) {
    if (!widget || !widget->iconLabel) continue;
    if (isPseudoHwnd(hwnd)) {
      const QString uniqueId = widget->property("unique_id").toString();
      const QPixmap icon = uniqueId.isEmpty() ? QPixmap() : pinnedIcon(uniqueId);
      if (!icon.isNull()) widget->iconLabel->setPixmap(icon);
      continue;
    }
    const auto entry = windowButtons_.find(hwnd);
    if (entry == windowButtons_.end()) continue;
    const QPixmap icon = appIcon(hwnd);
    entry->second.icon = icon;
    // A grouped button is shared by its windows, only the representing window's icon is shown.
    if (!icon.isNull() && widget->hwnd() == hwnd) widget->iconLabel->setPixmap(icon);
  }
}

bool TaskbarWidget::event(QEvent* event) {
  if (event->type() == QEvent::DevicePixelRatioChange) {
    const qreal dpr = devicePixelRatioF();
    if (!dpi_ || *dpi_ != dpr) {
      dpi_ = dpr;
      guarded("reload icons", [&] { reloadIcons(); });
    }
  }
  return BaseWidget::event(event);
}

// --- interaction

void TaskbarWidget::onButtonClicked(platform::WindowHandle hwnd, QWidget* button) {
  // Activate the window, or open the preview when the button holds several windows.
  std::vector<platform::WindowHandle> members = membersOf(hwnd);
  if (members.size() > 1) {
    if (previewEnabled_) {
      PreviewPopup* popup = previewManager_ ? previewManager_->popup() : nullptr;
      if (!(popup && popup->isVisible() && popup->srcHwnd() == hwnd))
        showPreviewForHwnd(hwnd, button);
      return;
    }
    // Without previews, cycle through the windows of the group.
    int active = -1;
    for (size_t i = 0; i < members.size(); ++i) {
      if (tracker_ && tracker_->isActive(members[i])) {
        active = static_cast<int>(i);
        break;
      }
    }
    hwnd = members[static_cast<size_t>(active + 1) % members.size()];
  }
  bringToForeground(hwnd);
}

void TaskbarWidget::showPreviewForHwnd(platform::WindowHandle hwnd, QWidget* anchor) {
  guarded("show preview", [&] {
    // A pinned app that is not running has no window to preview.
    if (isPseudoHwnd(hwnd)) return;
    if (previewEnabled_ && previewManager_)
      previewManager_->showPreviewForHwnds(membersOf(hwnd), anchor);
  });
}

void TaskbarWidget::hidePreview() {
  guarded("hide preview", [&] {
    if (previewManager_) previewManager_->hidePreview();
  });
}

// Bring the window to the foreground, restoring it if minimized.
void TaskbarWidget::bringToForeground(platform::WindowHandle hwnd) {
  // A pinned app that is not running is launched instead.
  if (isPseudoHwnd(hwnd)) {
    launchPinnedApp(hwnd);
    return;
  }
  // Stop flashing if this window was flashing.
  stopFlashTimer(hwnd);
  if (!system_->isWindow(hwnd)) return;
  try {
    hidePreview();

    const auto [base, focusTarget] = system_->resolveBaseAndFocus(hwnd);
    bool isActive = false;
    try {
      isActive = tracker_ && tracker_->isActive(base);
    } catch (...) {
    }
    if (!isActive) isActive = system_->isOwnerRootActive(base);

    if (system_->isIconic(base)) {
      system_->restoreWindow(base);
      system_->setForeground(base);
      const platform::WindowHandle target = focusTarget ? focusTarget : base;
      QTimer::singleShot(0, this, [this, target] { system_->setForeground(target); });
      return;
    }

    if (isActive) {
      if (system_->canMinimize(hwnd)) system_->minimizeWindow(base);
      return;
    }

    system_->showWindow(base);
    system_->setForeground(base);
  } catch (const std::exception& error) {
    qDebug().noquote() << "Could not bring window" << hwnd << "to the foreground:" << error.what();
    try {
      if (system_->isIconic(hwnd))
        system_->restoreWindow(hwnd);
      else
        system_->showWindow(hwnd);
    } catch (...) {
    }
  }
}

platform::WindowHandle TaskbarWidget::hwndUnderCursor() const {
  // The pointer may be over a child of the button, so walk up to the first widget with a window.
  for (QWidget* w = QApplication::widgetAt(QCursor::pos()); w; w = w->parentWidget()) {
    const QVariant hwnd = w->property("hwnd");
    if (hwnd.isValid() && hwnd.toULongLong() != 0)
      return static_cast<platform::WindowHandle>(hwnd.toULongLong());
  }
  return 0;
}

void TaskbarWidget::onToggleWindow() {
  const platform::WindowHandle hwnd = hwndUnderCursor();
  if (hwnd == 0 || !system_->isWindow(hwnd)) return;
  bringToForeground(hwnd);
}

void TaskbarWidget::onCloseApp() {
  hidePreview();
  const platform::WindowHandle hwnd = hwndUnderCursor();
  if (hwnd == 0) {
    qWarning() << "Taskbar close_app: no window under the cursor";
    return;
  }
  // The placeholder of a pinned app that is not running has nothing to close.
  if (isPseudoHwnd(hwnd)) return;
  if (system_->isWindow(hwnd)) {
    system_->closeApplication(hwnd, false);
  } else {
    qWarning().noquote() << "Invalid window handle" << hwnd << ", removing stale UI";
    guarded("stale ui", [&] { removeWindowUi(hwnd, std::nullopt, /*immediate=*/true); });
  }
}

void TaskbarWidget::onContextMenu() {
  hidePreview();
  const platform::WindowHandle hwnd = hwndUnderCursor();
  if (hwnd == 0) return;
  // Remove the hover state from the button.
  if (AppButton* container = buttonFor(hwnd)) {
    container->setAttribute(Qt::WA_UnderMouse, false);
    container->update();
  }
  showContextMenu(hwnd, QCursor::pos());
}

// --- pinned apps (PinManager)

QWidget* TaskbarWidget::layoutWidgetAt(int index) const {
  const QLayout* layout = labelContainer()->layout();
  if (!layout || index < 0 || index >= layout->count()) return nullptr;
  return layout->itemAt(index)->widget();
}

// The layout index of the button standing for a handle, -1 when none.
int TaskbarWidget::hwndPosition(platform::WindowHandle hwnd) const {
  const int count = buttonCount();
  for (int i = 0; i < count; ++i) {
    const QWidget* widget = layoutWidgetAt(i);
    if (widget && widget->property("hwnd").toULongLong() == hwnd) return i;
  }
  return -1;
}

// The index after every pinned button (pinned-only and running pinned), except the one of `skip`.
int TaskbarWidget::insertPositionAfterPinned(platform::WindowHandle skip) const {
  int position = 0;
  const int count = buttonCount();
  for (int i = 0; i < count; ++i) {
    const QWidget* widget = layoutWidgetAt(i);
    if (!widget) continue;
    const platform::WindowHandle hwnd = widget->property("hwnd").toULongLong();
    if (hwnd == 0) continue;
    if (isPseudoHwnd(hwnd))
      position = i + 1;
    else if (hwnd != skip && runningPinned_.count(hwnd))
      position = i + 1;
  }
  return position;
}

void TaskbarWidget::updatePinnedStatus(platform::WindowHandle hwnd, bool pinned) {
  AppButton* widget = buttonFor(hwnd);
  if (!widget) return;
  widget->setProperty("pinned", pinned);
  // The id the button's menu and "Open app" launch.
  const auto running = runningPinned_.find(hwnd);
  widget->setProperty("unique_id", pinned && running != runningPinned_.end()
                                       ? QVariant(running->second)
                                       : QVariant());
  const QString current = classOf(widget);
  const QString marker = QStringLiteral(" pinned");
  if (pinned && !current.contains(QLatin1String("pinned")))
    setClassProperty(widget, current + marker);
  else if (!pinned && current.contains(QLatin1String("pinned")))
    setClassProperty(widget, QString(current).replace(marker, QString()));
  refreshStyle({widget});
}

QString TaskbarWidget::pinnedTitle(const QString& uniqueId) const {
  const QString title = pinStore_->title(uniqueId);
  return title.isEmpty() ? QStringLiteral("App") : title;
}

AppButton* TaskbarWidget::pinnedOnlyButton(const QString& uniqueId) const {
  for (const auto& [hwnd, widget] : hwndToWidget_) {
    if (widget && isPseudoHwnd(hwnd) && widget->property("unique_id").toString() == uniqueId)
      return widget;
  }
  return nullptr;
}

QStringList TaskbarWidget::layoutUniqueIds() const {
  QStringList ids;
  const int count = buttonCount();
  for (int i = 0; i < count; ++i) {
    const QWidget* widget = layoutWidgetAt(i);
    ids << (widget ? widget->property("unique_id").toString() : QString());
  }
  return ids;
}

// A pinned app's icon: the bin's state for the Recycle Bin, otherwise loaded on the icon worker
// (blank until iconsChanged fires, then reloadIcons sets it).
QPixmap TaskbarWidget::pinnedIcon(const QString& uniqueId) {
  if (options_.icon_size <= 0) return {};
  if (platform::isRecycleBinId(uniqueId)) return recycleBinIcon(recycleBinEmpty_);
  const platform::PinMetadata metadata{pinStore_->title(uniqueId),
                                       pinStore_->shortcutPath(uniqueId)};
  const std::shared_ptr<platform::PinSupport> pins = pins_;
  return icons_->iconFor(pseudoHwndFor(uniqueId), options_.icon_size, dpi_.value_or(1.0),
                         [pins, uniqueId, metadata](int physical) {
                           return loadPinnedIcon(*pins, uniqueId, metadata, physical);
                         });
}

// The Recycle Bin's stock icon for the bin's state, cached per state and pixel ratio.
QPixmap TaskbarWidget::recycleBinIcon(bool empty) {
  if (options_.icon_size <= 0 || !dpi_) return {};
  const auto key = std::make_pair(empty, *dpi_);
  if (const auto cached = recycleBinIcons_.find(key); cached != recycleBinIcons_.end())
    return cached->second;
  const int physical = std::max(1, static_cast<int>(options_.icon_size * *dpi_));
  const QPixmap icon = TaskbarIcons::pixmapFromBitmap(pins_->recycleBinIcon(empty, physical),
                                                      options_.icon_size, *dpi_);
  if (!icon.isNull()) recycleBinIcons_[key] = icon;
  return icon;
}

void TaskbarWidget::startRecycleBinMonitor() {
  if (!recycleBin_) {
    recycleBin_ = new RecycleBinMonitor(pins_, 2000, asyncRecycleBin_, this);
    connect(recycleBin_, &RecycleBinMonitor::binUpdated, this, [this](qint64 numItems, qint64) {
      guarded("recycle bin update", [&] { onRecycleBinUpdate(numItems); });
    });
  }
  recycleBin_->start();
}

void TaskbarWidget::stopRecycleBinMonitor() {
  if (recycleBin_) recycleBin_->stop();
}

// Updates every Recycle Bin button's icon when the bin changes between empty and not.
void TaskbarWidget::onRecycleBinUpdate(qint64 numItems) {
  recycleBinEmpty_ = numItems == 0;
  const QPixmap icon = recycleBinIcon(recycleBinEmpty_);
  if (icon.isNull()) return;
  for (const auto& [hwnd, widget] : hwndToWidget_) {
    if (!widget || !widget->iconLabel) continue;
    if (platform::isRecycleBinId(widget->property("unique_id").toString()))
      widget->iconLabel->setPixmap(icon);
  }
}

// A button for a pinned app that is not running: only the icon, no title or count.
AppButton* TaskbarWidget::createPinnedContainer(const QString& title, const QPixmap& icon,
                                                platform::WindowHandle pseudo,
                                                const QString& uniqueId) {
  auto* container = new AppButton(this, pseudo);
  container->setProperty("class", "app-container");
  container->setProperty("hwnd", static_cast<qulonglong>(pseudo));
  container->setProperty("unique_id", uniqueId);
  container->setProperty("pinned", true);

  auto* outer = new QHBoxLayout(container);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(0);
  auto* wrapper = new QFrame;
  auto* content = new QHBoxLayout(wrapper);
  content->setContentsMargins(0, 0, 0, 0);
  content->setSpacing(0);

  if (options_.icon_size > 0) {
    auto* iconLabel = new QLabel;
    iconLabel->setProperty("class", "app-icon");
    iconLabel->setFixedSize(options_.icon_size, options_.icon_size);
    if (!icon.isNull()) iconLabel->setPixmap(icon);
    iconLabel->setProperty("hwnd", static_cast<qulonglong>(pseudo));
    content->addWidget(iconLabel);
    container->iconLabel = iconLabel;
  }
  outer->addWidget(wrapper);

  // The configured tooltip, not affected by the preview (a pinned-only button has none).
  if (tooltipEnabled_) setTooltip(container, title, globalState_, 0);
  return container;
}

void TaskbarWidget::createPinnedButton(const QString& uniqueId, int position) {
  const platform::WindowHandle pseudo = pseudoHwndFor(uniqueId);
  AppButton* container =
      createPinnedContainer(pinnedTitle(uniqueId), pinnedIcon(uniqueId), pseudo, uniqueId);
  hwndToWidget_[pseudo] = container;
  if (animationEnabled_) {
    container->setFixedWidth(0);
    insertButton(position, container);
    animateContainer(container, 0, container->sizeHint().width(), animationDuration_, pseudo);
  } else {
    insertButton(position, container);
  }
}

// Brings the pinned button back when its app closed.
void TaskbarWidget::recreatePinnedButton(const QString& uniqueId, int position) {
  pendingPinnedRecreations_.erase(uniqueId);
  // The app still runs, or is no longer pinned.
  const bool running = std::any_of(runningPinned_.begin(), runningPinned_.end(),
                                   [&](const auto& entry) { return entry.second == uniqueId; });
  if (running || !pinStore_->contains(uniqueId)) return;
  if (pinnedOnlyButton(uniqueId)) return;
  createPinnedButton(uniqueId, position >= 0 && position <= buttonCount() ? position : -1);
}

// Loads the pinned file and shows a button per pinned app (pinned apps are global, every bar shows
// all of them regardless of monitor_exclusive).
void TaskbarWidget::displayPinnedApps() {
  if (pinnedDisplayed_) return;
  pinnedDisplayed_ = true;
  pinStore_->load();
  const QStringList order = pinStore_->order();
  for (const QString& uniqueId : order) {
    if (!pinStore_->contains(uniqueId)) continue;
    if (pinnedOnlyButton(uniqueId)) continue;
    createPinnedButton(uniqueId);
    // Watch the bin when it is pinned.
    if (platform::isRecycleBinId(uniqueId)) startRecycleBinMonitor();
  }
}

void TaskbarWidget::pinApp(platform::WindowHandle hwnd) {
  guarded("pin app", [&] {
    const auto entry = windowButtons_.find(hwnd);
    if (entry == windowButtons_.end()) {
      qWarning().noquote() << "Taskbar: cannot pin, window" << hwnd << "is not shown";
      return;
    }
    const platform::WindowInfo info = entry->second.info;
    const std::optional<QString> uniqueId = system_->appUniqueId(info);
    if (!uniqueId) {
      qWarning() << "Taskbar: cannot pin the app, it could not be identified";
      return;
    }
    platform::PinMetadata metadata = pins_->resolveMetadata(info, *uniqueId);
    if (metadata.title.isEmpty()) metadata.title = info.title;
    QJsonObject json{{QStringLiteral("title"), metadata.title}};
    if (!metadata.shortcutPath.isEmpty())
      json.insert(QStringLiteral("shortcut_path"), metadata.shortcutPath);

    // Pin after the existing pinned apps and before the unpinned running ones.
    const int position = static_cast<int>(pinStore_->order().size());
    runningPinned_[hwnd] = *uniqueId;
    pinStore_->pin(*uniqueId, json, position);
    updatePinnedStatus(hwnd, true);

    // Move the button to the end of the pinned apps.
    if (AppButton* widget = buttonFor(hwnd)) {
      removeButton(widget);
      insertButton(insertPositionAfterPinned(), widget);
    }
  });
}

void TaskbarWidget::unpinApp(platform::WindowHandle hwnd) {
  guarded("unpin app", [&] {
    QString uniqueId;
    if (const auto running = runningPinned_.find(hwnd); running != runningPinned_.end()) {
      uniqueId = running->second;
    } else {
      const auto entry = windowButtons_.find(hwnd);
      if (entry == windowButtons_.end()) {
        qWarning().noquote() << "Taskbar: cannot unpin, window" << hwnd << "is not shown";
        return;
      }
      if (const auto id = system_->appUniqueId(entry->second.info)) uniqueId = *id;
    }
    if (uniqueId.isEmpty() || !pinStore_->contains(uniqueId)) return;
    runningPinned_.erase(hwnd);
    pinStore_->unpin(uniqueId);
    updatePinnedStatus(hwnd, false);
  });
}

void TaskbarWidget::unpinPinnedOnlyApp(const QString& uniqueId, platform::WindowHandle pseudoHwnd) {
  guarded("unpin pinned-only app", [&] {
    if (!pinStore_->contains(uniqueId)) return;
    // The store's "unpin" signal removes the button here and on every other taskbar.
    pinStore_->unpin(uniqueId);
    // In case the signal did not reach this widget.
    if (const auto found = hwndToWidget_.find(pseudoHwnd); found != hwndToWidget_.end()) {
      if (found->second) {
        removeButton(found->second);
        found->second->deleteLater();
      }
      hwndToWidget_.erase(found);
    }
    if (options_.hide_empty && hwndToWidget_.empty() && pendingPinnedRecreations_.empty())
      hideTaskbarWidget();
  });
}

void TaskbarWidget::launchPinnedApp(const QString& uniqueId, const QString& extraArguments) {
  guarded("launch pinned app", [&] {
    const platform::PinMetadata metadata{pinStore_->title(uniqueId),
                                         pinStore_->shortcutPath(uniqueId)};
    pins_->launch(uniqueId, metadata, extraArguments);
  });
}

void TaskbarWidget::launchPinnedApp(platform::WindowHandle hwnd) {
  // A pseudo or real handle: the button's unique id says what to launch.
  const AppButton* widget = buttonFor(hwnd);
  if (!widget) return;
  const QString uniqueId = widget->property("unique_id").toString();
  if (uniqueId.isEmpty()) return;
  launchPinnedApp(uniqueId, QString());
}

// Order of the pinned apps after a drag; pinned apps are global, so every taskbar shares it.
void TaskbarWidget::updatePinnedOrderFromLayout() {
  QStringList visibleOrder;
  const int count = buttonCount();
  for (int i = 0; i < count; ++i) {
    const QWidget* widget = layoutWidgetAt(i);
    if (!widget) continue;
    const platform::WindowHandle hwnd = widget->property("hwnd").toULongLong();
    if (hwnd == 0) continue;
    if (isPseudoHwnd(hwnd)) {
      const QString uniqueId = widget->property("unique_id").toString();
      if (!uniqueId.isEmpty() && pinStore_->contains(uniqueId)) visibleOrder << uniqueId;
    } else if (const auto running = runningPinned_.find(hwnd); running != runningPinned_.end()) {
      const QString& uniqueId = running->second;
      if (pinStore_->contains(uniqueId) && !visibleOrder.contains(uniqueId))
        visibleOrder << uniqueId;
    }
  }
  pinStore_->updateOrder(visibleOrder);
}

void TaskbarWidget::setDragging(bool active) {
  suspendUpdates_ = active;
  if (active) hidePreview();
}

// While a file is dragged over a button its window comes forward.
void TaskbarWidget::ensureForeground(platform::WindowHandle hwnd) {
  if (!system_->isWindow(hwnd)) return;
  try {
    const auto [base, focusTarget] = system_->resolveBaseAndFocus(hwnd);
    if (system_->isIconic(base)) system_->restoreWindow(base);
    system_->setForeground(focusTarget);
    // After the raise, reflect the expected focus at once.
    clearOthersSetForeground(base);
  } catch (...) {
    try {
      if (system_->isIconic(hwnd))
        system_->restoreWindow(hwnd);
      else
        system_->showWindow(hwnd);
    } catch (...) {
    }
  }
}

// Changes from the shared store (this taskbar's own pins and unpins included).
void TaskbarWidget::onPinnedAppsChanged(const QString& action, const QString& uniqueId) {
  pinStore_->load();

  if (action == QLatin1String("pin")) {
    // Watch the bin when it is being pinned.
    if (platform::isRecycleBinId(uniqueId)) startRecycleBinMonitor();
    if (!pinStore_->contains(uniqueId)) return;

    // A running window of the app becomes its pinned button.
    bool foundRunning = false;
    for (const auto& [hwnd, widget] :
         std::map<platform::WindowHandle, QPointer<AppButton>>(hwndToWidget_)) {
      if (!widget || isPseudoHwnd(hwnd)) continue;
      const auto entry = windowButtons_.find(hwnd);
      if (entry == windowButtons_.end()) continue;
      if (system_->appUniqueId(entry->second.info) != uniqueId) continue;
      runningPinned_[hwnd] = uniqueId;
      for (platform::WindowHandle member : groupMembers(uniqueId))
        runningPinned_[member] = uniqueId;
      updatePinnedStatus(hwnd, true);
      foundRunning = true;
      // Move the button to the end of the pinned apps, not counting itself.
      removeButton(widget);
      insertButton(insertPositionAfterPinned(hwnd), widget);
      break;
    }

    if (!foundRunning && !pinnedOnlyButton(uniqueId)) {
      createPinnedButton(uniqueId, insertPositionAfterPinned());
      // Show the taskbar when it was hidden.
      if (options_.hide_empty && !hwndToWidget_.empty()) showTaskbarWidget();
    }
  } else if (action == QLatin1String("unpin")) {
    if (platform::isRecycleBinId(uniqueId)) stopRecycleBinMonitor();

    // Remove the pinned-only button if it exists.
    if (AppButton* widget = pinnedOnlyButton(uniqueId)) {
      hwndToWidget_.erase(widget->hwnd());
      removeButton(widget);
      widget->deleteLater();
    }
    // Running windows of the app lose their pinned state.
    std::vector<platform::WindowHandle> unpinned;
    for (const auto& [hwnd, id] : runningPinned_) {
      if (id == uniqueId) unpinned.push_back(hwnd);
    }
    for (platform::WindowHandle hwnd : unpinned) {
      runningPinned_.erase(hwnd);
      updatePinnedStatus(hwnd, false);
    }
    if (options_.hide_empty && hwndToWidget_.empty() && pendingPinnedRecreations_.empty())
      hideTaskbarWidget();
  } else if (action == QLatin1String("reorder")) {
    // Rebuild the layout in the new order: take every button out, put the pinned ones back in the
    // shared order and the others after them.
    std::map<QString, AppButton*> pinnedWidgets;
    std::vector<AppButton*> others;
    auto* layout = qobject_cast<QBoxLayout*>(labelContainer()->layout());
    if (!layout) return;
    while (layout->count() > 0) {
      QLayoutItem* item = layout->takeAt(0);
      auto* taken = item ? qobject_cast<AppButton*>(item->widget()) : nullptr;
      delete item;
      if (!taken) continue;
      const platform::WindowHandle hwnd = taken->property("hwnd").toULongLong();
      QString id;
      if (isPseudoHwnd(hwnd)) {
        id = taken->property("unique_id").toString();
      } else if (const auto running = runningPinned_.find(hwnd); running != runningPinned_.end()) {
        id = running->second;
      }
      if (!id.isEmpty() && pinStore_->contains(id) && !pinnedWidgets.count(id))
        pinnedWidgets[id] = taken;
      else
        others.push_back(taken);
      taken->setParent(nullptr);
    }
    for (const QString& id : pinStore_->order()) {
      if (!pinStore_->contains(id)) continue;
      const auto found = pinnedWidgets.find(id);
      if (found != pinnedWidgets.end()) {
        insertButton(-1, found->second);
        pinnedWidgets.erase(found);
      } else {
        createPinnedButton(id);
      }
    }
    // A button whose app is no longer pinned goes with the unpinned ones.
    for (const auto& [id, widget] : pinnedWidgets) others.push_back(widget);
    for (AppButton* widget : others) insertButton(-1, widget);
  }
}

void registerTaskbarWidget(WidgetRegistry& registry) {
  registry.add("yasb.taskbar.TaskbarWidget", [](const WidgetContext& context) -> BaseWidget* {
    // One tracker serves every taskbar (the Python shared task manager).
    auto* tracker = context.pollingWorkers.service<platform::WindowTracker>("taskbar_windows", [] {
      return new platform::WindowTracker(platform::makeWin32WindowSystem(),
                                         platform::makeWin32TrackerHooks());
    });
    // Pinned apps are global, so every taskbar shares one store (Python's signal bus).
    auto* pins = context.pollingWorkers.service<PinStore>(
        "taskbar_pins", [] { return new PinStore(PinStore::defaultFile()); });
    TaskbarDeps deps;
    deps.system = tracker->system();
    deps.tracker = tracker;
    deps.pinStore = pins;
    return new TaskbarWidget(context, std::move(deps));
  });
}

}  // namespace yasb::widgets
