#include "widgets/glazewm/workspaces_widget.h"

#include <QBoxLayout>
#include <QDebug>
#include <QLabel>
#include <QMouseEvent>
#include <QScreen>
#include <QStyle>
#include <QWheelEvent>
#include <algorithm>
#include <exception>
#include <string>

#include "core/widgets/label_format.h"
#include "platform/komorebi.h"
#include "widgets/taskbar/taskbar_icons.h"

namespace yasb::widgets {
namespace {

void repolish(QWidget* widget) {
  if (QStyle* style = widget->style()) {
    style->unpolish(widget);
    style->polish(widget);
  }
}

// One piece of re.split(r"(\d+)", s): text pieces sit at even positions, digit runs at odd ones.
struct NaturalToken {
  bool number = false;
  std::u32string text;  // lowercase text, or the digit run as ASCII digits without leading zeros
};

std::vector<NaturalToken> naturalKey(const QString& s) {
  std::vector<NaturalToken> key;
  NaturalToken piece;
  const std::u32string chars = s.toLower().toStdU32String();
  size_t i = 0;
  while (i < chars.size()) {
    const char32_t c = chars[i];
    if (QChar::category(c) == QChar::Number_DecimalDigit) {
      key.push_back(piece);
      piece = NaturalToken();
      std::u32string digits;
      while (i < chars.size() && QChar::category(chars[i]) == QChar::Number_DecimalDigit) {
        const int value = QChar::digitValue(chars[i]);
        digits.push_back(static_cast<char32_t>(U'0' + (value < 0 ? 0 : value)));
        ++i;
      }
      const size_t first = digits.find_first_not_of(U'0');
      digits = first == std::u32string::npos ? U"0" : digits.substr(first);
      key.push_back({true, digits});
    } else {
      piece.text.push_back(c);
      ++i;
    }
  }
  key.push_back(piece);
  return key;
}

bool tokenLess(const NaturalToken& a, const NaturalToken& b) {
  if (a.number != b.number) return a.number;  // never happens: positions are typed
  if (a.number && a.text.size() != b.text.size()) return a.text.size() < b.text.size();
  return a.text < b.text;
}

using WorkspaceList = std::vector<std::pair<QString, const glazewm::Workspace*>>;

// dict assignment: a repeated key keeps its first position and takes the new value.
void setEntry(WorkspaceList& list, const QString& name, const glazewm::Workspace* workspace) {
  for (auto& entry : list) {
    if (entry.first == name) {
      entry.second = workspace;
      return;
    }
  }
  list.emplace_back(name, workspace);
}

const glazewm::Workspace* findEntry(const WorkspaceList& list, const QString& name) {
  for (const auto& entry : list) {
    if (entry.first == name) return entry.second;
  }
  return nullptr;
}

}  // namespace

QString glazewmStatusName(GlazewmWorkspaceStatus status) {
  switch (status) {
    case GlazewmWorkspaceStatus::Populated:
      return QStringLiteral("populated");
    case GlazewmWorkspaceStatus::ActiveEmpty:
      return QStringLiteral("active_empty");
    case GlazewmWorkspaceStatus::ActivePopulated:
      return QStringLiteral("active_populated");
    case GlazewmWorkspaceStatus::FocusedEmpty:
      return QStringLiteral("focused_empty");
    case GlazewmWorkspaceStatus::FocusedPopulated:
      return QStringLiteral("focused_populated");
    case GlazewmWorkspaceStatus::Empty:
      break;
  }
  return QStringLiteral("empty");
}

bool glazewmNaturalLess(const QString& a, const QString& b) {
  const std::vector<NaturalToken> left = naturalKey(a);
  const std::vector<NaturalToken> right = naturalKey(b);
  return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end(),
                                      tokenLess);
}

GlazewmWorkspaceButtonBase::GlazewmWorkspaceButtonBase(
    const config::options::GlazewmWorkspacesConfig& options)
    : monitorExclusive(options.monitor_exclusive), options_(options) {}

void GlazewmWorkspaceButtonBase::updateStatus() {
  const bool populated = windowCount > 0;
  if (isFocused) {
    status_ =
        populated ? GlazewmWorkspaceStatus::FocusedPopulated : GlazewmWorkspaceStatus::FocusedEmpty;
  } else if (monitorExclusive && isDisplayed) {
    status_ =
        populated ? GlazewmWorkspaceStatus::ActivePopulated : GlazewmWorkspaceStatus::ActiveEmpty;
  } else {
    status_ = populated ? GlazewmWorkspaceStatus::Populated : GlazewmWorkspaceStatus::Empty;
  }
}

void GlazewmWorkspaceButtonBase::updateLabel() {
  const nlohmann::json replacements = {{"name", name.toStdString()},
                                       {"display_name", displayName.toStdString()}};
  // Label priority: YASB config -> display_name from GlazeWM -> name from GlazeWM. Python `or`
  // skips None and "".
  const auto pick = [this](const std::optional<std::string>& configured) {
    if (configured && !configured->empty()) return QString::fromStdString(*configured);
    return displayName.isEmpty() ? name : displayName;
  };
  const QString populated = pick(options_.populated_label);
  const QString empty = pick(options_.empty_label);
  const QString activePopulated = pick(options_.active_populated_label);
  const QString activeEmpty = pick(options_.active_empty_label);
  // focused_ variants fall back to the equivalent active_ variants (previous behaviour).
  const auto focusedPick = [&](const std::optional<std::string>& configured,
                               const QString& active) {
    if (configured && !configured->empty()) return QString::fromStdString(*configured);
    return active.isEmpty() ? pick(std::nullopt) : active;
  };
  const QString focusedPopulated = focusedPick(options_.focused_populated_label, activePopulated);
  const QString focusedEmpty = focusedPick(options_.focused_empty_label, activeEmpty);

  // Python formats all six before choosing one, so a bad template in any raises.
  const auto format = [&](const QString& templ) {
    return QString::fromStdString(pyFormat(templ.toStdString(), replacements));
  };
  const QString populatedText = format(populated);
  const QString emptyText = format(empty);
  const QString activePopulatedText = format(activePopulated);
  const QString activeEmptyText = format(activeEmpty);
  const QString focusedPopulatedText = format(focusedPopulated);
  const QString focusedEmptyText = format(focusedEmpty);

  switch (status_) {
    case GlazewmWorkspaceStatus::FocusedPopulated:
      setLabel(focusedPopulatedText);
      break;
    case GlazewmWorkspaceStatus::FocusedEmpty:
      setLabel(focusedEmptyText);
      break;
    case GlazewmWorkspaceStatus::ActivePopulated:
      setLabel(activePopulatedText);
      break;
    case GlazewmWorkspaceStatus::ActiveEmpty:
      setLabel(activeEmptyText);
      break;
    case GlazewmWorkspaceStatus::Populated:
      setLabel(populatedText);
      break;
    case GlazewmWorkspaceStatus::Empty:
      setLabel(emptyText);
      return;  // an empty workspace keeps its visibility
  }
  widget()->setHidden(false);
}

void GlazewmWorkspaceButtonBase::updateButton() {
  updateStatus();
  updateLabel();
  updateIcons();
  // The equivalent active_ class comes first on a focused_ workspace (previous behaviour).
  QString cssClass = QStringLiteral("ws-btn ");
  if (status_ == GlazewmWorkspaceStatus::FocusedPopulated) {
    cssClass += glazewmStatusName(GlazewmWorkspaceStatus::ActivePopulated) + QLatin1Char(' ') +
                glazewmStatusName(status_);
  } else if (status_ == GlazewmWorkspaceStatus::FocusedEmpty) {
    cssClass += glazewmStatusName(GlazewmWorkspaceStatus::ActiveEmpty) + QLatin1Char(' ') +
                glazewmStatusName(status_);
  } else {
    cssClass += glazewmStatusName(status_);
  }
  widget()->setProperty("class", cssClass);
  repolishAll();
}

GlazewmWorkspaceButton::GlazewmWorkspaceButton(
    const QString& workspaceName, const QString& workspaceDisplayName,
    const config::options::GlazewmWorkspacesConfig& options, QWidget* parent)
    : QPushButton(parent), GlazewmWorkspaceButtonBase(options) {
  name = workspaceName;
  displayName = workspaceDisplayName;
  setProperty("class", QStringLiteral("ws-btn"));
  connect(this, &QPushButton::clicked, this, [this] { runActivate(); });
  QSizePolicy policy = sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Fixed);
  setSizePolicy(policy);
}

void GlazewmWorkspaceButton::repolishAll() { repolish(this); }

GlazewmIconButton::GlazewmIconButton(const QString& workspaceName,
                                     const QString& workspaceDisplayName,
                                     const config::options::GlazewmWorkspacesConfig& options,
                                     IconLookup lookup, std::vector<glazewm::Window> windows,
                                     QWidget* parent)
    : QFrame(parent),
      GlazewmWorkspaceButtonBase(options),
      lookup_(std::move(lookup)),
      windows_(std::move(windows)) {
  name = workspaceName;
  displayName = workspaceDisplayName;
  setProperty("class", QStringLiteral("ws-btn"));
  QSizePolicy policy = sizePolicy();
  policy.setHorizontalPolicy(QSizePolicy::Fixed);
  setSizePolicy(policy);

  layout_ = new QHBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);

  text_ = new QLabel(name, this);
  text_->setProperty("class", QStringLiteral("label"));
  text_->setAlignment(Qt::AlignCenter);
  layout_->addWidget(text_);

  updateButton();
}

QString GlazewmIconButton::labelText() const { return text_->text(); }

void GlazewmIconButton::setLabel(const QString& text) { text_->setText(text); }

void GlazewmIconButton::repolishAll() {
  repolish(this);
  // The label class name never changes, but the label styling may depend on the status.
  repolish(text_);
}

void GlazewmIconButton::mousePressEvent(QMouseEvent* event) {
  if (event && event->button() == Qt::LeftButton) runActivate();
}

void GlazewmIconButton::updateIcons() {
  const auto& app = options_.app_icons;
  icons_.clear();
  uniquePids_.clear();
  for (const glazewm::Window& window : windows_) {
    if (app.hide_floating && window.isFloating) continue;
    const QPixmap pixmap = lookup_ ? lookup_(window.handle, &uniquePids_, screen()) : QPixmap();
    // dict semantics: a repeated handle keeps its first position.
    auto found = std::find_if(icons_.begin(), icons_.end(),
                              [&](const auto& entry) { return entry.first == window.handle; });
    if (found != icons_.end()) {
      found->second = pixmap;
    } else {
      icons_.emplace_back(window.handle, pixmap);
    }
  }

  const GlazewmWorkspaceStatus current = status();
  std::vector<QPixmap> list;
  if (!app.enabled_active && current == GlazewmWorkspaceStatus::ActivePopulated) {
    // no icons on an active workspace
  } else if (((!app.enabled_active && !app.enabled_focused.has_value()) ||
              (app.enabled_focused.has_value() && !*app.enabled_focused)) &&
             current == GlazewmWorkspaceStatus::FocusedPopulated) {
    // no icons on the focused workspace
  } else if (!app.enabled_populated && current == GlazewmWorkspaceStatus::Populated) {
    // no icons on the other workspaces
  } else {
    for (const auto& entry : icons_) {
      if (!entry.second.isNull()) list.push_back(entry.second);
    }
    if (app.max_icons > 0 && list.size() > static_cast<size_t>(app.max_icons)) {
      list.resize(static_cast<size_t>(app.max_icons));
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
      auto* label = new QLabel(this);
      label->setProperty("class", QStringLiteral("icon icon-%1").arg(index + 1));
      label->setPixmap(list[index]);
      layout_->addWidget(label);
      iconLabels_.push_back(label);
    }
  }

  if (app.hide_label && !iconLabels_.empty()) {
    text_->hide();
  } else {
    text_->show();
  }
}

GlazewmWorkspacesWidget::GlazewmWorkspacesWidget(const WidgetContext& context)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 QStringLiteral("glazewm-workspaces"), 0),
      bus_(context.eventBus),
      options_(std::get<config::options::GlazewmWorkspacesConfig>(context.typedOptions)) {
  auto* frameLayout = static_cast<QBoxLayout*>(widgetFrame()->layout());
  offline_ = new QLabel(QString::fromStdString(options_.offline_label), widgetFrame());
  offline_->setProperty("class", QStringLiteral("offline-status"));
  frameLayout->insertWidget(0, offline_);
  labelContainer()->hide();  // the workspace container, shown once GlazeWM is online

  iconsEnabled_ = appIconsEnabled();
  if (iconsEnabled_) setWindowSystem(platform::makeWin32WindowSystem(), /*async=*/true);

  connect(&bus_, &EventBus::event, this, &GlazewmWorkspacesWidget::onBusEvent);
}

GlazewmWorkspacesWidget::~GlazewmWorkspacesWidget() = default;

bool GlazewmWorkspacesWidget::appIconsEnabled() const {
  // Python truthiness of `enabled_populated or enabled_active or enabled_focused` (None is falsy).
  return options_.app_icons.enabled_populated || options_.app_icons.enabled_active ||
         options_.app_icons.enabled_focused.value_or(false);
}

void GlazewmWorkspacesWidget::setWindowSystem(std::shared_ptr<platform::WindowSystem> system,
                                              bool async) {
  windowSystem_ = std::move(system);
  if (icons_) {
    icons_->disconnect(this);
    icons_->deleteLater();
    icons_ = nullptr;
  }
  if (!windowSystem_) return;
  icons_ = new TaskbarIcons(windowSystem_, async, this);
  connect(icons_, &TaskbarIcons::iconsChanged, this, &GlazewmWorkspacesWidget::refreshIcons);
}

std::optional<long long> GlazewmWorkspacesWidget::resolveMonitor() const {
  if (monitorResolver_) return monitorResolver_();
  if (QWidget* top = window(); top && top != this) {
    return platform::monitorFromWindow(top->winId());
  }
  return std::nullopt;
}

void GlazewmWorkspacesWidget::showEvent(QShowEvent* event) {
  BaseWidget::showEvent(event);
  monitorHandle_ = resolveMonitor();
  if (!subscribed_) {
    subscribed_ = true;
    glazewm::subscribe(bus_, glazewm::kTopicWorkspaces,
                       QString::fromStdString(options_.glazewm_server_uri));
  }
  // Python's client.connect() moves the socket to Connecting, which reports "not connected".
  if (!online_) onConnection(false);
}

void GlazewmWorkspacesWidget::onBusEvent(const QString& name, const QVariantMap& payload) {
  if (name != glazewm::kStateEvent) return;
  try {
    const QString kind = payload.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("connection")) {
      onConnection(payload.value(QStringLiteral("connected")).toBool());
    } else if (kind == QStringLiteral("workspaces")) {
      if (const glazewm::MonitorsPtr monitors = glazewm::monitorsFromPayload(payload)) {
        onWorkspaces(monitors);
      }
    }
  } catch (const std::exception& error) {
    qWarning().noquote() << "glazewm workspaces update failed:" << error.what();
  }
}

void GlazewmWorkspacesWidget::onConnection(bool connected) {
  online_ = connected;
  labelContainer()->setVisible(connected);
  offline_->setVisible(options_.hide_if_offline ? false : !connected);
}

GlazewmWorkspaceButtonBase* GlazewmWorkspacesWidget::buttonByName(const QString& name) const {
  for (GlazewmWorkspaceButtonBase* button : buttons_) {
    if (button->name == name) return button;
  }
  return nullptr;
}

std::vector<GlazewmWorkspaceButtonBase*> GlazewmWorkspacesWidget::layoutButtons() const {
  std::vector<GlazewmWorkspaceButtonBase*> ordered;
  const QLayout* layout = labelContainer()->layout();
  for (int i = 0; i < layout->count(); ++i) {
    const QLayoutItem* item = layout->itemAt(i);
    const QWidget* widget = item ? item->widget() : nullptr;
    for (GlazewmWorkspaceButtonBase* button : buttons_) {
      if (button->widget() == widget) ordered.push_back(button);
    }
  }
  return ordered;
}

GlazewmWorkspaceButtonBase* GlazewmWorkspacesWidget::createButton(
    const glazewm::Workspace& workspace) {
  GlazewmWorkspaceButtonBase* button = nullptr;
  if (iconsEnabled_) {
    button = new GlazewmIconButton(
        workspace.name, workspace.displayName, options_,
        [this](long long hwnd, std::set<std::uint32_t>* pids, const QScreen* screen) {
          return appIcon(hwnd, pids, screen);
        },
        workspace.windows, this);
  } else {
    button = new GlazewmWorkspaceButton(workspace.name, workspace.displayName, options_, this);
  }
  const QString name = workspace.name;
  button->setActivateHandler([this, name] { glazewm::activateWorkspace(bus_, name); });
  buttons_.push_back(button);
  return button;
}

void GlazewmWorkspacesWidget::onWorkspaces(const glazewm::MonitorsPtr& monitors) {
  if (!monitors || !monitorHandle_) return;
  const glazewm::Monitor* current = nullptr;
  for (const glazewm::Monitor& monitor : *monitors) {
    if (monitor.hwnd == *monitorHandle_) {
      current = &monitor;
      break;
    }
  }
  if (!current) return;

  std::optional<QString> globalFocused;
  WorkspaceList all;
  for (const glazewm::Monitor& monitor : *monitors) {
    for (const glazewm::Workspace& workspace : monitor.workspaces) {
      setEntry(all, workspace.name, &workspace);
      if (workspace.focus) globalFocused = workspace.name;
    }
  }

  WorkspaceList source;
  if (options_.monitor_exclusive) {
    for (const glazewm::Workspace& workspace : current->workspaces) {
      setEntry(source, workspace.name, &workspace);
    }
  } else {
    source = all;
  }

  // `name == global_focused_ws if global_focused_ws else workspace.focus`
  const auto focusedFor = [&](const QString& name, const glazewm::Workspace& workspace) {
    if (globalFocused && !globalFocused->isEmpty()) return name == *globalFocused;
    return workspace.focus;
  };
  const auto assign = [&](GlazewmWorkspaceButtonBase* button, const glazewm::Workspace& workspace) {
    button->displayName = workspace.displayName;
    button->windowCount = workspace.numWindows;
    button->isDisplayed = workspace.isDisplayed;
    button->isFocused = focusedFor(button->name, workspace);
    if (auto* iconButton = dynamic_cast<GlazewmIconButton*>(button)) {
      iconButton->setWindows(workspace.windows);
    }
  };

  for (const auto& [name, workspace] : source) {
    GlazewmWorkspaceButtonBase* button = buttonByName(name);
    if (!button) button = createButton(*workspace);
    button->monitorExclusive = options_.monitor_exclusive;
    button->name = workspace->name;
    assign(button, *workspace);
  }

  std::vector<GlazewmWorkspaceButtonBase*> sorted = buttons_;
  std::stable_sort(sorted.begin(), sorted.end(),
                   [](const GlazewmWorkspaceButtonBase* a, const GlazewmWorkspaceButtonBase* b) {
                     return glazewmNaturalLess(a->name, b->name);
                   });
  auto* layout = static_cast<QBoxLayout*>(labelContainer()->layout());
  for (int i = 0; i < static_cast<int>(sorted.size()); ++i) {
    QWidget* widget = sorted[static_cast<size_t>(i)]->widget();
    if (layout->indexOf(widget) == i) continue;
    layout->removeWidget(widget);
    layout->insertWidget(i, widget);
  }

  for (GlazewmWorkspaceButtonBase* button : buttons_) {
    button->monitorExclusive = options_.monitor_exclusive;
    const glazewm::Workspace* workspace = findEntry(source, button->name);
    const bool isCurrent = workspace != nullptr;
    if (isCurrent) {
      assign(button, *workspace);
    } else {
      button->isDisplayed = false;
      button->windowCount = 0;
      button->isFocused = false;
      if (auto* iconButton = dynamic_cast<GlazewmIconButton*>(button)) iconButton->setWindows({});
    }

    if (!options_.monitor_exclusive) {
      button->widget()->setHidden(!(isCurrent || button->isFocused));
    } else {
      button->widget()->setHidden(!isCurrent);
    }
    button->updateButton();
  }
}

void GlazewmWorkspacesWidget::scrollWorkspaces(int deltaY) {
  if (!options_.enable_scroll_switching) return;
  int direction = deltaY;
  if (options_.reverse_scroll_direction) direction = -direction;
  if (direction < 0) {
    if (options_.monitor_exclusive) {
      glazewm::focusNextWorkspace(bus_);
    } else {
      glazewm::focusNextWorkspaceGlobal(bus_);
    }
  } else if (direction > 0) {
    if (options_.monitor_exclusive) {
      glazewm::focusPrevWorkspace(bus_);
    } else {
      glazewm::focusPrevWorkspaceGlobal(bus_);
    }
  }
}

void GlazewmWorkspacesWidget::wheelEvent(QWheelEvent* event) {
  if (event) scrollWorkspaces(event->angleDelta().y());
}

QPixmap GlazewmWorkspacesWidget::appIcon(long long hwnd, std::set<std::uint32_t>* uniquePids,
                                         const QScreen* screen) {
  try {
    if (!icons_ || !windowSystem_) return {};
    const auto handle = static_cast<platform::WindowHandle>(hwnd);
    if (options_.app_icons.hide_duplicates && uniquePids) {
      const std::uint32_t pid = windowSystem_->query(handle).pid;
      if (!uniquePids->insert(pid).second) return {};
    }
    if (!screen) return {};
    return icons_->icon(handle, options_.app_icons.size, screen->devicePixelRatio());
  } catch (const std::exception& error) {
    qDebug().noquote() << "glazewm workspaces: failed to get icon for window" << hwnd
                       << error.what();
    return {};
  }
}

void GlazewmWorkspacesWidget::refreshIcons() {
  for (GlazewmWorkspaceButtonBase* button : buttons_) {
    if (auto* iconButton = dynamic_cast<GlazewmIconButton*>(button)) iconButton->refreshIcons();
  }
}

}  // namespace yasb::widgets
