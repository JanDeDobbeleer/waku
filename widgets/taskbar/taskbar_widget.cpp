#include "widgets/taskbar/taskbar_widget.h"

#include <QApplication>
#include <QCursor>
#include <QDebug>
#include <QEasingCurve>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSizePolicy>
#include <QStyle>
#include <QStyleFactory>
#include <algorithm>
#include <exception>

#include "core/bar/theme_class.h"
#include "core/widgets/backdrop.h"
#include "core/widgets/polling_worker_registry.h"
#include "core/widgets/tooltip.h"
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
    if (taskbar_->previewEnabled()) {
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
    if (taskbar_->previewEnabled()) taskbar_->showPreviewForHwnd(hwnd_, this);
  });
}

void AppButton::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    event->accept();
    return;
  }
  // The other buttons fall through to the widget's callbacks (context menu, middle click).
  QFrame::mousePressEvent(event);
}

void AppButton::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (rect().contains(event->position().toPoint())) {
      guarded("button click", [&] { taskbar_->onButtonClicked(hwnd_, this); });
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
      tooltip_(options_.tooltip && !options_.preview.enabled) {
  icons_ = deps.icons ? deps.icons : new TaskbarIcons(system_);
  icons_->setParent(this);
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

  guarded("connect tracker", [&] {
    if (tracker_ && !trackerConnected_) {
      // start() delivers the already open windows synchronously.
      initialPopulation_ = true;
      connectTracker();
      trackerConnected_ = true;
      initialPopulation_ = false;
    }
  });

  if (hwndToWidget_.empty() && options_.hide_empty) {
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
    return icons_->icon(hwnd, options_.icon_size, *dpi_);
  } catch (const std::exception& error) {
    qDebug().noquote() << "Failed to get the icon of window" << hwnd << error.what();
    return {};
  }
}

void TaskbarWidget::addWindowUi(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  // Skip a window that is already shown (prevents duplicate calls).
  if (windowButtons_.count(hwnd)) return;
  // Apps already minimized when the button appears; minimize events keep it current after.
  if (system_->isIconic(hwnd)) minimizedHwnds_.insert(hwnd);

  const std::optional<QString> uniqueId = system_->appUniqueId(info);
  const QString title = info.title;
  const QString process = info.processName.value_or(QString());

  // Another window of the same app is already on the bar: attach it to that button.
  QString groupKey;
  if (groupingEnabled_ && uniqueId) groupKey = *uniqueId;
  if (!groupKey.isEmpty()) {
    const auto group = groupHwnds_.find(groupKey);
    if (group != groupHwnds_.end() && !group->second.empty()) {
      if (AppButton* container = buttonFor(group->second.front())) {
        group->second.push_back(hwnd);
        hwndToGroup_[hwnd] = groupKey;
        hwndToWidget_[hwnd] = container;
        windowButtons_[hwnd] = WindowEntry{title, appIcon(hwnd), hwnd, process};
        syncGroupButton(groupKey);
        return;
      }
      groupHwnds_.erase(group);
    }
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
  windowButtons_[hwnd] = WindowEntry{title, icon, hwnd, process};

  AppButton* container = createAppContainer(title, icon, hwnd);
  hwndToWidget_[hwnd] = container;
  if (!groupKey.isEmpty()) {
    groupHwnds_[groupKey] = {hwnd};
    hwndToGroup_[hwnd] = groupKey;
  }

  // Animating the whole startup burst at once is what stutters, so skip it for the initial
  // population and only animate windows added after that.
  const int position = buttonCount();
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
  icons_->forget(hwnd);

  // Grouped button: drop only this window while other windows of the app stay open.
  if (const auto grouped = hwndToGroup_.find(hwnd); grouped != hwndToGroup_.end()) {
    const QString groupKey = grouped->second;
    hwndToGroup_.erase(grouped);
    auto& members = groupHwnds_[groupKey];
    members.erase(std::remove(members.begin(), members.end(), hwnd), members.end());
    if (!members.empty()) {
      animatingWidgets_.erase(hwnd);
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

  // Some apps (the Nvidia App) send remove events with no process info when they close.
  if (info && !info->processName) {
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

  if (options_.hide_empty && hwndToWidget_.empty()) hideTaskbarWidget();
}

void TaskbarWidget::updateWindowUi(platform::WindowHandle hwnd, const platform::WindowInfo& info) {
  const QString title = info.title;
  const QPixmap icon = appIcon(hwnd);
  windowButtons_[hwnd] = WindowEntry{title, icon, hwnd, info.processName.value_or(QString())};

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
      if (options_.hide_empty && hwndToWidget_.empty()) hideTaskbarWidget();
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

QMenu* TaskbarWidget::showContextMenu(platform::WindowHandle hwnd, const QPoint& pos) {
  contextMenuOpen_ = false;
  QMenu* menu = nullptr;
  try {
    menu = new QMenu(window());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    // apply_qmenu_style: Fusion removes the menu shadow, blur and round corners on show.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
      fusion->setParent(menu);
      menu->setStyle(fusion);
    }
    connect(menu, &QMenu::aboutToShow, menu, [menu] {
      try {
        enableBlur(menu->winId(), false, true, QStringLiteral("normal"), QStringLiteral("None"));
      } catch (...) {
      }
    });
    menu->setProperty("class", "context-menu");

    if (system_->isWindow(hwnd)) {
      connect(menu->addAction(QStringLiteral("End task")), &QAction::triggered, this,
              [this, hwnd] { system_->closeApplication(hwnd, true); });
      connect(menu->addAction(QStringLiteral("Close window")), &QAction::triggered, this,
              [this, hwnd] { system_->closeApplication(hwnd, false); });
      const std::vector<platform::WindowHandle> members = membersOf(hwnd);
      if (members.size() > 1) {
        connect(menu->addAction(QStringLiteral("Close all windows")), &QAction::triggered, this,
                [this, members] {
                  for (platform::WindowHandle member : members)
                    system_->closeApplication(member, false);
                });
      }
    }
    if (menu->isEmpty()) {
      delete menu;
      return nullptr;
    }

    // Place the menu just outside the bar, centred on the button.
    const int margin = 6;
    const QSize size = menu->sizeHint();
    QWidget* bar = window();
    const QPoint barTopLeft = bar ? bar->mapToGlobal(bar->rect().topLeft()) : QPoint();
    const int barHeight = bar ? bar->height() : 0;
    const AppButton* button = buttonFor(hwnd);
    const QPoint center = button ? button->mapToGlobal(button->rect().center()) : pos;
    const int x = center.x() - size.width() / 2;
    const QScreen* screen = QApplication::screenAt(center);
    const bool barOnTop =
        screen && bar && bar->frameGeometry().center().y() < screen->geometry().center().y();
    const int y =
        barOnTop ? barTopLeft.y() + barHeight + margin : barTopLeft.y() - size.height() - margin;
    // popup() rather than exec() lets focus be handled properly.
    menu->popup(QPoint(x, y));
    menu->activateWindow();

    contextMenuOpen_ = true;
    connect(menu, &QMenu::aboutToHide, this, [this, hwnd] {
      contextMenuOpen_ = false;
      if (options_.title_label.enabled && options_.title_label.show == "focused") {
        for (const auto& [key, widget] :
             std::map<platform::WindowHandle, QPointer<AppButton>>(hwndToWidget_))
          refreshTitleVisibility(key);
      } else {
        refreshTitleVisibility(hwnd);
      }
    });
    return menu;
  } catch (const std::exception& error) {
    qWarning().noquote() << "Taskbar context menu failed:" << error.what();
    delete menu;
    return nullptr;
  }
}

void registerTaskbarWidget(WidgetRegistry& registry) {
  registry.add("yasb.taskbar.TaskbarWidget", [](const WidgetContext& context) -> BaseWidget* {
    // One tracker serves every taskbar (the Python shared task manager).
    auto* tracker = context.pollingWorkers.service<platform::WindowTracker>("taskbar_windows", [] {
      return new platform::WindowTracker(platform::makeWin32WindowSystem(),
                                         platform::makeWin32TrackerHooks());
    });
    TaskbarDeps deps;
    deps.system = tracker->system();
    deps.tracker = tracker;
    return new TaskbarWidget(context, std::move(deps));
  });
}

}  // namespace yasb::widgets
