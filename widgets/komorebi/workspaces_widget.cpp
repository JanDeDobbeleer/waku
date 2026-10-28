#include "widgets/komorebi/workspaces_widget.h"

#include <QBoxLayout>
#include <QDebug>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QStyle>
#include <QWheelEvent>
#include <algorithm>
#include <set>
#include <string>

#include "core/widgets/label_format.h"
#include "platform/komorebi.h"
#include "widgets/komorebi/komorebi_state.h"
#include "widgets/taskbar/taskbar_icons.h"

namespace yasb::widgets {
namespace {

using komorebi::child;
using komorebi::Json;
using komorebi::JsonPtr;

const std::set<std::string>& focusEvents() {
  static const std::set<std::string> events = {
      "CycleFocusWorkspace", "CycleFocusMonitor",    "FocusMonitorWorkspaceNumber",
      "FocusMonitorNumber",  "FocusWorkspaceNumber", "ToggleWorkspaceLayer"};
  return events;
}

const std::set<std::string>& buttonUpdateEvents() {
  static const std::set<std::string> events = {"EnsureWorkspaces",
                                               "Manage",
                                               "MoveContainerToWorkspaceNumber",
                                               "NewWorkspace",
                                               "ReloadConfiguration",
                                               "SendContainerToMonitorNumber",
                                               "SendContainerToWorkspaceNumber",
                                               "Unmanage",
                                               "WatchConfiguration",
                                               "WorkspaceName",
                                               "Cloak"};
  return events;
}

std::string eventType(const Json* event) {
  const Json* type = child(event, "type");
  return type && type->is_string() ? type->get<std::string>() : std::string();
}

void repolish(QWidget* widget) {
  if (QStyle* style = widget->style()) {
    style->unpolish(widget);
    style->polish(widget);
  }
}

QString statusClass(WorkspaceStatus status) {
  switch (status) {
    case WorkspaceStatus::Active:
      return QStringLiteral("active");
    case WorkspaceStatus::Populated:
      return QStringLiteral("populated");
    case WorkspaceStatus::Empty:
      break;
  }
  return QStringLiteral("empty");
}

}  // namespace

WorkspaceButtonBase::WorkspaceButtonBase(int workspaceIndex, const QString& defaultLabel,
                                         const QString& activeLabel, const QString& populatedLabel)
    : workspaceIndex_(workspaceIndex) {
  // Python: `label if label else str(index + 1)`; empty active/populated fall back to the default.
  defaultLabel_ = defaultLabel.isEmpty() ? QString::number(workspaceIndex + 1) : defaultLabel;
  activeLabel_ = activeLabel.isEmpty() ? defaultLabel_ : activeLabel;
  populatedLabel_ = populatedLabel.isEmpty() ? defaultLabel_ : populatedLabel;
}

void WorkspaceButtonBase::setLabels(const QString& defaultLabel, const QString& activeLabel,
                                    const QString& populatedLabel) {
  defaultLabel_ = defaultLabel;
  activeLabel_ = activeLabel;
  populatedLabel_ = populatedLabel;
}

void WorkspaceButtonBase::setClass(const QString& cssClass) {
  widget()->setProperty("class", cssClass);
  repolish(widget());
}

void WorkspaceButtonBase::setStatusClass(WorkspaceStatus status) {
  status_ = status;
  widget()->setProperty("class", QStringLiteral("ws-btn ") + statusClass(status));
}

const QString& WorkspaceButtonBase::labelFor(WorkspaceStatus status) const {
  switch (status) {
    case WorkspaceStatus::Active:
      return activeLabel_;
    case WorkspaceStatus::Populated:
      return populatedLabel_;
    case WorkspaceStatus::Empty:
      break;
  }
  return defaultLabel_;
}

WorkspaceButton::WorkspaceButton(int workspaceIndex, const QString& defaultLabel,
                                 const QString& activeLabel, const QString& populatedLabel,
                                 QWidget* parent)
    : QPushButton(parent),
      WorkspaceButtonBase(workspaceIndex, defaultLabel, activeLabel, populatedLabel) {
  setProperty("class", QStringLiteral("ws-btn"));
  setText(defaultLabel_);
  connect(this, &QPushButton::clicked, this, [this] { runActivate(); });
  QSizePolicy policy = sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Fixed);
  setSizePolicy(policy);
  hide();
  updateAndRedraw(status_);
}

void WorkspaceButton::updateAndRedraw(WorkspaceStatus status) {
  setStatusClass(status);
  setText(labelFor(status));
  repolish(this);
}

WorkspaceIconButton::WorkspaceIconButton(int workspaceIndex, const QString& defaultLabel,
                                         const QString& activeLabel, const QString& populatedLabel,
                                         const config::options::KomorebiAppIconsConfig& options,
                                         IconSource allIcons, IconLookup oneIcon,
                                         CurrentIndex currentIndex, QWidget* parent)
    : QFrame(parent),
      WorkspaceButtonBase(workspaceIndex, defaultLabel, activeLabel, populatedLabel),
      options_(options),
      allIcons_(std::move(allIcons)),
      oneIcon_(std::move(oneIcon)),
      currentIndex_(std::move(currentIndex)) {
  setProperty("class", QStringLiteral("ws-btn"));
  QSizePolicy policy = sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Fixed);
  setSizePolicy(policy);

  layout_ = new QHBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);

  text_ = new QLabel(defaultLabel_);
  text_->setProperty("class", QStringLiteral("label"));
  text_->setAlignment(Qt::AlignCenter);
  layout_->addWidget(text_);

  hide();
  updateIcons();
  updateAndRedraw(status_);
}

QString WorkspaceIconButton::labelText() const { return text_->text(); }

void WorkspaceIconButton::mousePressEvent(QMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton) runActivate();
}

void WorkspaceIconButton::updateAndRedraw(WorkspaceStatus status) {
  setStatusClass(status);
  text_->setText(labelFor(status));
  repolish(this);
}

void WorkspaceIconButton::updateIcons(const WorkspaceIcons* icons) {
  if (icons && !icons->empty()) {
    for (const auto& [hwnd, pixmap] : *icons) {
      auto found = std::find_if(icons_.begin(), icons_.end(),
                                [hwnd = hwnd](const auto& entry) { return entry.first == hwnd; });
      if (found != icons_.end()) {
        found->second = pixmap;
      } else {
        icons_.emplace_back(hwnd, pixmap);
      }
    }
  } else {
    icons_ = allIcons_ ? allIcons_(workspaceIndex_) : WorkspaceIcons();
  }

  const bool isCurrent = currentIndex_ && currentIndex_() == workspaceIndex_;
  std::vector<QPixmap> list;
  if (!options_.enabled_active && isCurrent) {
    // no icons on the focused workspace
  } else if (!options_.enabled_populated && !isCurrent) {
    // no icons on the other workspaces
  } else {
    for (const auto& entry : icons_) {
      if (!entry.second.isNull()) list.push_back(entry.second);
    }
    if (options_.max_icons > 0 && list.size() > static_cast<size_t>(options_.max_icons)) {
      list.resize(static_cast<size_t>(options_.max_icons));
    }
  }

  // Remove extra QLabels if there are more than needed.
  while (iconLabels_.size() > list.size()) {
    QLabel* extra = iconLabels_.back();
    iconLabels_.pop_back();
    layout_->removeWidget(extra);
    extra->hide();
    extra->deleteLater();
  }
  // Add or update icons.
  for (size_t index = 0; index < list.size(); ++index) {
    if (index < iconLabels_.size()) {
      iconLabels_[index]->setPixmap(list[index]);
    } else {
      auto* label = new QLabel();
      label->setProperty("class", QStringLiteral("icon icon-%1").arg(index + 1));
      label->setPixmap(list[index]);
      layout_->addWidget(label);
      iconLabels_.push_back(label);
    }
  }

  if (options_.hide_label && !iconLabels_.empty()) {
    text_->hide();
  } else {
    text_->show();
  }
}

void WorkspaceIconButton::updateIconByHwnd(long long hwnd) {
  const bool known = std::any_of(icons_.begin(), icons_.end(),
                                 [hwnd](const auto& entry) { return entry.first == hwnd; });
  if (!known || !oneIcon_) return;
  const QPixmap pixmap = oneIcon_(hwnd);
  if (pixmap.isNull()) return;
  const WorkspaceIcons update = {{hwnd, pixmap}};
  updateIcons(&update);
}

KomorebiWorkspacesWidget::KomorebiWorkspacesWidget(const WidgetContext& context, bool subscribe)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 QStringLiteral("komorebi-workspaces"), 0),
      bus_(context.eventBus),
      options_(std::get<config::options::KomorebiWorkspacesConfig>(context.typedOptions)) {
  if (options_.hide_if_offline) hide();

  auto* frameLayout = static_cast<QBoxLayout*>(widgetFrame()->layout());
  offline_ = new QLabel(QString::fromStdString(options_.label_offline), widgetFrame());
  offline_->setProperty("class", QStringLiteral("offline-status"));
  frameLayout->insertWidget(0, offline_);
  labelContainer()->hide();  // the workspace container, shown once komorebi is online

  floatOverride_ = new QLabel(QString::fromStdString(options_.label_float_override), widgetFrame());
  floatOverride_->setProperty("class", QStringLiteral("float-override"));
  floatOverride_->hide();
  frameLayout->addWidget(floatOverride_);

  if (options_.toggle_workspace_layer.enabled) {
    layerLabel_ = new QLabel(widgetFrame());
    layerLabel_->setProperty("class", QStringLiteral("workspace-layer"));
    frameLayout->addWidget(layerLabel_);
  }

  if (appIconsEnabled()) setWindowSystem(platform::makeWin32WindowSystem(), /*async=*/true);

  connect(&bus_, &EventBus::event, this, &KomorebiWorkspacesWidget::onBusEvent);
  if (subscribe) emit bus_.event(komorebi::kSubscribeEvent, {});
}

KomorebiWorkspacesWidget::~KomorebiWorkspacesWidget() = default;

bool KomorebiWorkspacesWidget::appIconsEnabled() const {
  return options_.app_icons.enabled_populated || options_.app_icons.enabled_active;
}

void KomorebiWorkspacesWidget::setWindowSystem(std::shared_ptr<platform::WindowSystem> system,
                                               bool async) {
  windowSystem_ = std::move(system);
  if (icons_) {
    icons_->disconnect(this);
    icons_->deleteLater();
    icons_ = nullptr;
  }
  if (!windowSystem_) return;
  icons_ = new TaskbarIcons(windowSystem_, async, this);
  connect(icons_, &TaskbarIcons::iconsChanged, this, &KomorebiWorkspacesWidget::refreshIcons);
}

bool KomorebiWorkspacesWidget::event(QEvent* event) {
  const bool handled = BaseWidget::event(event);
  if (event->type() == QEvent::ParentChange || event->type() == QEvent::Show) retryPending();
  return handled;
}

void KomorebiWorkspacesWidget::retryPending() {
  if (!pending_ || !resolveMonitor()) return;
  JsonPtr state = std::move(pending_);
  pending_.reset();
  try {
    onConnect(std::move(state));
  } catch (const std::exception& error) {
    qWarning().noquote() << "komorebi workspaces update failed:" << error.what();
  }
}

void KomorebiWorkspacesWidget::onBusEvent(const QString& name, const QVariantMap& payload) {
  if (name != komorebi::kStateEvent) return;
  try {
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("connect")) {
      onConnect(komorebi::jsonFromPayload(payload, QStringLiteral("state")));
    } else if (kind == QStringLiteral("update")) {
      onUpdate(komorebi::jsonFromPayload(payload, QStringLiteral("event")),
               komorebi::jsonFromPayload(payload, QStringLiteral("state")));
    } else if (kind == QStringLiteral("disconnect")) {
      onDisconnect();
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "komorebi workspaces update failed:" << error.what();
  }
}

void KomorebiWorkspacesWidget::onConnect(JsonPtr state) {
  reset();
  hideOffline();
  pending_.reset();
  if (updateState(state)) {
    addOrUpdateButtons();
  } else if (!resolveMonitor() && state && komorebi::truthy(state.get())) {
    // The widget is not in a bar window yet, so the monitor is unknown: keep the state and
    // replay it once the widget is parented or shown.
    pending_ = state;
  }
  if (options_.hide_if_offline) show();
}

void KomorebiWorkspacesWidget::onDisconnect() {
  pending_.reset();
  showOffline();
  if (options_.hide_if_offline) hide();
}

void KomorebiWorkspacesWidget::reset() {
  state_.reset();
  screen_ = nullptr;
  workspaceCount_ = 0;
  current_.reset();
  previous_.reset();
  buttons_.clear();
  clearContainer();
}

void KomorebiWorkspacesWidget::clearContainer() {
  QLayout* layout = labelContainer()->layout();
  for (int i = layout->count() - 1; i >= 0; --i) {
    QLayoutItem* item = layout->itemAt(i);
    QWidget* widget = item ? item->widget() : nullptr;
    if (!widget) continue;
    layout->removeWidget(widget);
    widget->hide();
    widget->deleteLater();
  }
}

std::optional<long long> KomorebiWorkspacesWidget::resolveMonitor() const {
  if (monitorResolver_) return monitorResolver_();
  if (QWidget* top = window(); top && top != this) {
    return platform::monitorFromWindow(top->winId());
  }
  return std::nullopt;
}

bool KomorebiWorkspacesWidget::updateState(const JsonPtr& state) {
  const std::optional<long long> monitor = resolveMonitor();
  if (!monitor) return false;
  if (!state || !komorebi::truthy(state.get())) return false;
  state_ = state;
  int index = 0;
  screen_ = komorebi::screenById(*state_, *monitor, &index);
  if (!screen_) return false;
  screenIndex_ = index;
  workspaceCount_ = komorebi::workspaceCount(screen_);
  if (const auto focused = komorebi::focusedWorkspaceIndex(screen_)) {
    previous_ = current_;
    current_ = focused;
  }
  currentWindows_.resize(static_cast<size_t>(workspaceCount_), 0);
  previousWindows_ = currentWindows_;
  for (int i = 0; i < workspaceCount_; ++i) {
    currentWindows_[static_cast<size_t>(i)] =
        komorebi::windowCount(komorebi::workspaceAt(screen_, i), options_.app_icons.hide_floating);
  }
  return true;
}

void KomorebiWorkspacesWidget::onUpdate(JsonPtr event, JsonPtr state) {
  const std::string type = eventType(event.get());
  const QString typeText = QString::fromStdString(type);
  const bool updated = updateState(state);
  if (!updated && !resolveMonitor() && state && komorebi::truthy(state.get())) pending_ = state;
  if (updated && screen_) {
    if (appIconsEnabled()) updateIconsForEvent(type, event.get());
    if (type == "MoveWorkspaceToMonitorNumber") {
      if (komorebi::integer(child(event.get(), "content")) != screenIndex_) hideUnknownButtons();
      addOrUpdateButtons();
    } else if (focusEvents().count(type) || indexChanged()) {
      sendWorkspaceUpdate(typeText);
      WorkspaceButtonBase* previous = buttonAt(previous_);
      if (!previous) {
        addOrUpdateButtons();
      } else {
        updateButton(previous);
        if (WorkspaceButtonBase* current = buttonAt(current_)) {
          updateButton(current);
        } else {
          addOrUpdateButtons();
        }
      }
    } else if (buttonUpdateEvents().count(type)) {
      addOrUpdateButtons();
    }

    // A workspace that just became empty must lose its populated look.
    for (int i = 0; i < workspaceCount_; ++i) {
      const auto at = static_cast<size_t>(i);
      if (at < previousWindows_.size() && at < currentWindows_.size() &&
          previousWindows_[at] != currentWindows_[at] && currentWindows_[at] == 0 &&
          at < buttons_.size()) {
        updateButton(buttons_[at]);
      }
    }

    if (type == "CloseWorkspace") {
      hideUnknownButtons();
      addOrUpdateButtons();
    }
    if (type == "FocusChange") updateWorkspaceLayer(current_);

    if (komorebi::truthy(child(state.get(), "float_override")) &&
        !options_.label_float_override.empty()) {
      floatOverride_->show();
    } else {
      floatOverride_->hide();
    }
  }

  if (type == "MoveWindow" || type == "Show" || type == "Hide" || type == "Destroy") {
    sendWorkspaceUpdate(typeText);
  }
}

void KomorebiWorkspacesWidget::sendWorkspaceUpdate(const QString& eventType) {
  emit bus_.event(QStringLiteral("workspace_update"), {{QStringLiteral("type"), eventType}});
}

WorkspaceButtonBase* KomorebiWorkspacesWidget::buttonAt(std::optional<int> index) const {
  if (!index || *index < 0 || *index >= static_cast<int>(buttons_.size())) return nullptr;
  return buttons_[static_cast<size_t>(*index)];
}

void KomorebiWorkspacesWidget::updateButton(WorkspaceButtonBase* button) {
  if (!button || !screen_) return;
  refreshButtonLabels(button);
  const int index = button->workspaceIndex();
  const Json* workspace = komorebi::workspaceAt(screen_, index);
  if (!workspace) return;
  WorkspaceStatus status = WorkspaceStatus::Empty;
  if (current_ == index) {
    status = WorkspaceStatus::Active;
  } else if (komorebi::hasWindows(workspace)) {
    status = WorkspaceStatus::Populated;
  }
  if (options_.hide_empty_workspaces && status == WorkspaceStatus::Empty) {
    button->widget()->hide();
  } else {
    if (button->status() != status) button->updateAndRedraw(status);
    button->widget()->show();
    updateVisibleButtons();
  }
  updateWorkspaceLayer(index);
}

void KomorebiWorkspacesWidget::refreshButtonLabels(WorkspaceButtonBase* button) {
  // Names can change at runtime (`komorebic workspace-name`), so cached labels are refreshed.
  const auto labels = workspaceLabels(button->workspaceIndex());
  if (!labels) return;
  if (button->defaultLabel() == labels->normal && button->activeLabel() == labels->active &&
      button->populatedLabel() == labels->populated) {
    return;
  }
  button->setLabels(labels->normal, labels->active, labels->populated);
  button->updateAndRedraw(button->status());
}

std::optional<KomorebiWorkspacesWidget::Labels> KomorebiWorkspacesWidget::workspaceLabels(
    int workspaceIndex) const {
  if (!screen_) return std::nullopt;
  const Json* workspace = komorebi::workspaceAt(screen_, workspaceIndex);
  const long long index = options_.label_zero_index ? workspaceIndex : workspaceIndex + 1;
  const long long monitorIndex = options_.label_zero_index ? screenIndex_ : screenIndex_ + 1;

  std::string name;
  if (const Json* raw = child(workspace, "name"); raw && raw->is_string()) {
    name = raw->get<std::string>();
  }
  try {
    if (name.empty()) {
      name = pyFormat(options_.label_default_name,
                      Json{{"index", index}, {"monitor_index", monitorIndex}});
    }
  } catch (const std::exception&) {
    name = std::to_string(index);
  }
  const Json kwargs = {{"name", name}, {"index", index}, {"monitor_index", monitorIndex}};
  try {
    Labels labels;
    labels.normal = QString::fromStdString(pyFormat(options_.label_workspace_btn, kwargs));
    labels.active = QString::fromStdString(pyFormat(options_.label_workspace_active_btn, kwargs));
    labels.populated =
        QString::fromStdString(pyFormat(options_.label_workspace_populated_btn, kwargs));
    return labels;
  } catch (const std::exception& error) {
    qWarning().noquote() << "komorebi workspace label format failed:" << error.what();
    return std::nullopt;
  }
}

void KomorebiWorkspacesWidget::addOrUpdateButtons() {
  bool added = false;
  for (int i = 0; i < workspaceCount_; ++i) {
    if (i < static_cast<int>(buttons_.size())) {
      updateButton(buttons_[static_cast<size_t>(i)]);
    } else {
      tryAddButton(i);
      added = true;
    }
  }
  if (!added) return;
  std::stable_sort(buttons_.begin(), buttons_.end(),
                   [](const WorkspaceButtonBase* a, const WorkspaceButtonBase* b) {
                     return a->workspaceIndex() < b->workspaceIndex();
                   });
  auto* layout = static_cast<QBoxLayout*>(labelContainer()->layout());
  for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
    WorkspaceButtonBase* button = buttons_[static_cast<size_t>(i)];
    if (layout->indexOf(button->widget()) != i) layout->insertWidget(i, button->widget());
    updateButton(button);
  }
}

WorkspaceButtonBase* KomorebiWorkspacesWidget::tryAddButton(int workspaceIndex) {
  for (const WorkspaceButtonBase* existing : buttons_) {
    if (existing->workspaceIndex() == workspaceIndex) return nullptr;
  }
  Labels labels;
  if (const auto computed = workspaceLabels(workspaceIndex)) labels = *computed;
  WorkspaceButtonBase* button = nullptr;
  if (appIconsEnabled()) {
    button = new WorkspaceIconButton(
        workspaceIndex, labels.normal, labels.active, labels.populated, options_.app_icons,
        [this](int index) { return allIconsInWorkspace(index); },
        [this](long long hwnd) { return appIcon(hwnd); }, [this] { return current_; },
        labelContainer());
  } else {
    button = new WorkspaceButton(workspaceIndex, labels.normal, labels.active, labels.populated,
                                 labelContainer());
  }
  button->setActivateHandler([this, workspaceIndex] { activateWorkspace(workspaceIndex); });
  buttons_.push_back(button);
  return button;
}

void KomorebiWorkspacesWidget::hideUnknownButtons() {
  for (const WorkspaceButtonBase* button : std::vector<WorkspaceButtonBase*>(buttons_)) {
    const int index = button->workspaceIndex();
    if (index < workspaceCount_) continue;
    if (WorkspaceButtonBase* target = buttonAt(index)) target->widget()->hide();
  }
}

void KomorebiWorkspacesWidget::updateVisibleButtons() {
  int number = 0;
  for (WorkspaceButtonBase* button : buttons_) {
    if (button->widget()->isHidden()) continue;
    ++number;
    QStringList classes;
    for (const QString& cls : button->widget()->property("class").toString().split(
             QLatin1Char(' '), Qt::SkipEmptyParts)) {
      if (!cls.startsWith(QStringLiteral("button-"))) classes.append(cls);
    }
    classes.append(QStringLiteral("button-%1").arg(number));
    button->setClass(classes.join(QLatin1Char(' ')));
  }
}

void KomorebiWorkspacesWidget::updateWorkspaceLayer(std::optional<int> workspaceIndex) {
  if (!layerLabel_) return;
  const Json* workspace =
      screen_ && workspaceIndex ? komorebi::workspaceAt(screen_, *workspaceIndex) : nullptr;
  const Json* layer = child(workspace, "layer");
  if (komorebi::truthy(workspace) && layer && layer->is_string()) {
    const std::string name = layer->get<std::string>();
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    layerLabel_->setProperty("class", QString::fromStdString("workspace-layer " + lower));
    if (name == "Tiling") {
      layerLabel_->setText(QString::fromStdString(options_.toggle_workspace_layer.tiling_label));
    } else if (name == "Floating") {
      layerLabel_->setText(QString::fromStdString(options_.toggle_workspace_layer.floating_label));
    }
  } else {
    layerLabel_->setProperty("class", QStringLiteral("workspace-layer"));
    layerLabel_->setText(QString());
  }
  repolish(layerLabel_);
}

void KomorebiWorkspacesWidget::showOffline() {
  offline_->show();
  labelContainer()->hide();
  if (layerLabel_) layerLabel_->hide();
}

void KomorebiWorkspacesWidget::hideOffline() {
  offline_->hide();
  labelContainer()->show();
  if (layerLabel_) layerLabel_->show();
}

void KomorebiWorkspacesWidget::activateWorkspace(int workspaceIndex) {
  if (!screen_) return;
  komorebi::sendCommand(bus_, {QStringLiteral("focus-monitor-workspace"),
                               QString::number(screenIndex_), QString::number(workspaceIndex)});
}

void KomorebiWorkspacesWidget::scrollWorkspaces(int deltaY) {
  if (!options_.enable_scroll_switching || !screen_) return;
  const int direction = ((deltaY > 0) != options_.reverse_scroll_direction) ? -1 : 1;
  const int count = komorebi::workspaceCount(screen_);
  if (count == 0 || !current_) return;
  const int next = ((*current_ + direction) % count + count) % count;
  komorebi::sendCommand(bus_, {QStringLiteral("focus-monitor-workspace"),
                               QString::number(screenIndex_), QString::number(next)});
}

WorkspaceIconButton* KomorebiWorkspacesWidget::iconButtonAt(std::optional<int> index) const {
  return dynamic_cast<WorkspaceIconButton*>(buttonAt(index));
}

void KomorebiWorkspacesWidget::updateIconsForEvent(const std::string& type, const Json* event) {
  // Python wraps this block in try/except (IndexError, TypeError): the first failing lookup
  // abandons the rest of the block.
  const auto at = [this](std::optional<int> index) { return iconButtonAt(index); };
  if (type == "ToggleFloat") {
    WorkspaceIconButton* button = at(current_);
    if (!button) return;
    button->updateIcons();
  }
  if (indexChanged()) {
    WorkspaceIconButton* previous = at(previous_);
    if (!previous) return;
    previous->updateIcons();
    WorkspaceIconButton* current = at(current_);
    if (!current) return;
    current->updateIcons();
  }
  for (int i = 0; i < workspaceCount_; ++i) {
    const auto index = static_cast<size_t>(i);
    if (index >= previousWindows_.size() || index >= currentWindows_.size()) return;
    if (previousWindows_[index] != currentWindows_[index]) {
      WorkspaceIconButton* button = at(i);
      if (!button) return;
      button->updateIcons();
    } else if (type == "TitleUpdate") {
      WorkspaceIconButton* button = at(i);
      if (!button) return;
      const Json* content = child(event, "content");
      if (!content || !content->is_array() || content->size() < 2) return;
      const auto hwnd = komorebi::integer(child(&content->at(1), "hwnd"));
      if (!hwnd) return;
      button->updateIconByHwnd(*hwnd);
    }
  }
}

WorkspaceIcons KomorebiWorkspacesWidget::allIconsInWorkspace(int workspaceIndex) {
  WorkspaceIcons pixmaps;
  const Json* workspace = screen_ ? komorebi::workspaceAt(screen_, workspaceIndex) : nullptr;
  if (!workspace) return pixmaps;
  uniquePids_.clear();
  for (const long long hwnd : komorebi::windowHwnds(workspace, options_.app_icons.hide_floating)) {
    QPixmap pixmap = appIcon(hwnd);
    // dict semantics: a repeated hwnd keeps its first position.
    auto found = std::find_if(pixmaps.begin(), pixmaps.end(),
                              [hwnd](const auto& entry) { return entry.first == hwnd; });
    if (found != pixmaps.end()) {
      found->second = pixmap;
    } else {
      pixmaps.emplace_back(hwnd, pixmap);
    }
  }
  // A window whose icon is still loading (or missing) keeps what the button already shows.
  if (const auto* existing = dynamic_cast<WorkspaceIconButton*>(
          workspaceIndex >= 0 && workspaceIndex < static_cast<int>(buttons_.size())
              ? buttons_[static_cast<size_t>(workspaceIndex)]
              : nullptr)) {
    for (auto& [hwnd, pixmap] : pixmaps) {
      if (!pixmap.isNull()) continue;
      for (const auto& [oldHwnd, oldPixmap] : existing->icons()) {
        if (oldHwnd == hwnd) pixmap = oldPixmap;
      }
    }
  }
  return pixmaps;
}

QPixmap KomorebiWorkspacesWidget::appIcon(long long hwnd) {
  try {
    if (!icons_ || !windowSystem_) return {};
    const auto handle = static_cast<platform::WindowHandle>(hwnd);
    if (options_.app_icons.hide_duplicates) {
      const std::uint32_t pid = windowSystem_->query(handle).pid;
      if (!uniquePids_.insert(pid).second) return {};
    }
    const QScreen* targetScreen = screen();
    if (!targetScreen) return {};
    return icons_->icon(handle, options_.app_icons.size, targetScreen->devicePixelRatio());
  } catch (const std::exception& error) {
    qDebug().noquote() << "komorebi workspaces: failed to get icon for window" << hwnd
                       << error.what();
    return {};
  }
}

void KomorebiWorkspacesWidget::refreshIcons() {
  for (WorkspaceButtonBase* button : buttons_) {
    auto* iconButton = dynamic_cast<WorkspaceIconButton*>(button);
    if (iconButton && iconButton->workspaceIndex() < workspaceCount_) iconButton->updateIcons();
  }
}

void KomorebiWorkspacesWidget::wheelEvent(QWheelEvent* event) {
  scrollWorkspaces(event->angleDelta().y());
}

}  // namespace yasb::widgets
