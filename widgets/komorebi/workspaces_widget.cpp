#include "widgets/komorebi/workspaces_widget.h"

#include <QBoxLayout>
#include <QDebug>
#include <QLabel>
#include <QStyle>
#include <QWheelEvent>
#include <algorithm>
#include <set>
#include <string>

#include "core/widgets/label_format.h"
#include "platform/komorebi.h"
#include "widgets/komorebi/komorebi_state.h"

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

WorkspaceButton::WorkspaceButton(int workspaceIndex, const QString& defaultLabel,
                                 const QString& activeLabel, const QString& populatedLabel,
                                 QWidget* parent)
    : QPushButton(parent), workspaceIndex_(workspaceIndex) {
  setProperty("class", QStringLiteral("ws-btn"));
  // Python: `label if label else str(index + 1)`; empty active/populated fall back to the default.
  defaultLabel_ = defaultLabel.isEmpty() ? QString::number(workspaceIndex + 1) : defaultLabel;
  activeLabel_ = activeLabel.isEmpty() ? defaultLabel_ : activeLabel;
  populatedLabel_ = populatedLabel.isEmpty() ? defaultLabel_ : populatedLabel;
  setText(defaultLabel_);
  QSizePolicy policy = sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Fixed);
  setSizePolicy(policy);
  hide();
  updateAndRedraw(status_);
}

void WorkspaceButton::setLabels(const QString& defaultLabel, const QString& activeLabel,
                                const QString& populatedLabel) {
  defaultLabel_ = defaultLabel;
  activeLabel_ = activeLabel;
  populatedLabel_ = populatedLabel;
}

void WorkspaceButton::setClass(const QString& cssClass) {
  setProperty("class", cssClass);
  repolish(this);
}

void WorkspaceButton::updateAndRedraw(WorkspaceStatus status) {
  status_ = status;
  setProperty("class", QStringLiteral("ws-btn ") + statusClass(status));
  switch (status) {
    case WorkspaceStatus::Active:
      setText(activeLabel_);
      break;
    case WorkspaceStatus::Populated:
      setText(populatedLabel_);
      break;
    case WorkspaceStatus::Empty:
      setText(defaultLabel_);
      break;
  }
  repolish(this);
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

  if (options_.app_icons.enabled_populated || options_.app_icons.enabled_active) {
    qWarning() << "komorebi workspaces: app_icons are not supported by the native build yet;"
                  " showing plain workspace buttons";
  }

  connect(&bus_, &EventBus::event, this, &KomorebiWorkspacesWidget::onBusEvent);
  if (subscribe) emit bus_.event(komorebi::kSubscribeEvent, {});
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
  if (updateState(state)) addOrUpdateButtons();
  if (options_.hide_if_offline) show();
}

void KomorebiWorkspacesWidget::onDisconnect() {
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

bool KomorebiWorkspacesWidget::updateState(const JsonPtr& state) {
  std::optional<long long> monitor;
  if (monitorResolver_) {
    monitor = monitorResolver_();
  } else if (QWidget* top = window(); top && top != this) {
    monitor = platform::monitorFromWindow(top->winId());
  }
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
  if (updateState(state) && screen_) {
    if (type == "MoveWorkspaceToMonitorNumber") {
      if (komorebi::integer(child(event.get(), "content")) != screenIndex_) hideUnknownButtons();
      addOrUpdateButtons();
    } else if (focusEvents().count(type) || indexChanged()) {
      sendWorkspaceUpdate(typeText);
      WorkspaceButton* previous = buttonAt(previous_);
      if (!previous) {
        addOrUpdateButtons();
      } else {
        updateButton(previous);
        if (WorkspaceButton* current = buttonAt(current_)) {
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

WorkspaceButton* KomorebiWorkspacesWidget::buttonAt(std::optional<int> index) const {
  if (!index || *index < 0 || *index >= static_cast<int>(buttons_.size())) return nullptr;
  return buttons_[static_cast<size_t>(*index)];
}

void KomorebiWorkspacesWidget::updateButton(WorkspaceButton* button) {
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
    button->hide();
  } else {
    if (button->status() != status) button->updateAndRedraw(status);
    button->show();
    updateVisibleButtons();
  }
  updateWorkspaceLayer(index);
}

void KomorebiWorkspacesWidget::refreshButtonLabels(WorkspaceButton* button) {
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
                   [](const WorkspaceButton* a, const WorkspaceButton* b) {
                     return a->workspaceIndex() < b->workspaceIndex();
                   });
  auto* layout = static_cast<QBoxLayout*>(labelContainer()->layout());
  for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
    WorkspaceButton* button = buttons_[static_cast<size_t>(i)];
    if (layout->indexOf(button) != i) layout->insertWidget(i, button);
    updateButton(button);
  }
}

WorkspaceButton* KomorebiWorkspacesWidget::tryAddButton(int workspaceIndex) {
  for (const WorkspaceButton* existing : buttons_) {
    if (existing->workspaceIndex() == workspaceIndex) return nullptr;
  }
  Labels labels;
  if (const auto computed = workspaceLabels(workspaceIndex)) labels = *computed;
  auto* button = new WorkspaceButton(workspaceIndex, labels.normal, labels.active, labels.populated,
                                     labelContainer());
  connect(button, &QPushButton::clicked, this,
          [this, workspaceIndex] { activateWorkspace(workspaceIndex); });
  buttons_.push_back(button);
  return button;
}

void KomorebiWorkspacesWidget::hideUnknownButtons() {
  for (const WorkspaceButton* button : std::vector<WorkspaceButton*>(buttons_)) {
    const int index = button->workspaceIndex();
    if (index < workspaceCount_) continue;
    if (WorkspaceButton* target = buttonAt(index)) target->hide();
  }
}

void KomorebiWorkspacesWidget::updateVisibleButtons() {
  int number = 0;
  for (WorkspaceButton* button : buttons_) {
    if (button->isHidden()) continue;
    ++number;
    QStringList classes;
    for (const QString& cls :
         button->property("class").toString().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
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

void KomorebiWorkspacesWidget::wheelEvent(QWheelEvent* event) {
  scrollWorkspaces(event->angleDelta().y());
}

}  // namespace yasb::widgets
