#include "widgets/komorebi/active_layout_widget.h"

#include <QBoxLayout>
#include <QDebug>
#include <QFrame>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>
#include <map>

#include "platform/komorebi.h"
#include "widgets/komorebi/komorebi_state.h"

namespace yasb::widgets {
namespace {

using komorebi::child;
using komorebi::Json;
using komorebi::JsonPtr;

// komorebi's layout name -> `komorebic workspace-layout` argument.
const std::map<std::string, std::string>& layoutCommands() {
  static const std::map<std::string, std::string> map = {
      {"BSP", "bsp"},
      {"Columns", "columns"},
      {"Rows", "rows"},
      {"Grid", "grid"},
      {"Scrolling", "scrolling"},
      {"VerticalStack", "vertical-stack"},
      {"HorizontalStack", "horizontal-stack"},
      {"UltrawideVerticalStack", "ultrawide-vertical-stack"},
      {"RightMainVerticalStack", "right-main-vertical-stack"}};
  return map;
}

// komorebi's layout name -> layout_icons key.
const std::map<std::string, std::string>& layoutIconKeys() {
  static const std::map<std::string, std::string> map = {
      {"BSP", "bsp"},
      {"Columns", "columns"},
      {"Rows", "rows"},
      {"Grid", "grid"},
      {"Scrolling", "scrolling"},
      {"VerticalStack", "vertical_stack"},
      {"HorizontalStack", "horizontal_stack"},
      {"UltrawideVerticalStack", "ultrawide_vertical_stack"},
      {"RightMainVerticalStack", "right_main_vertical_stack"}};
  return map;
}

// Python str.title() on a snake_case layout name.
QString titleCase(const QString& snake) {
  QStringList words = snake.split(QLatin1Char('_'));
  for (QString& word : words) {
    if (!word.isEmpty()) word = word.at(0).toUpper() + word.mid(1).toLower();
  }
  return words.join(QLatin1Char(' '));
}

// Menu row: a frame that runs `action` on mouse press (Python overrides mousePressEvent).
class MenuItem final : public QFrame {
 public:
  explicit MenuItem(std::function<void()> action) : action_(std::move(action)) {}

 protected:
  void mousePressEvent(QMouseEvent*) override {
    if (action_) action_();
  }

 private:
  std::function<void()> action_;
};

}  // namespace

KomorebiActiveLayoutWidget::KomorebiActiveLayoutWidget(const WidgetContext& context, bool subscribe)
    : BaseWidget(QString::fromStdString(context.name), context.eventBus,
                 QStringLiteral("komorebi-active-layout"), 0),
      bus_(context.eventBus),
      globalState_(context.globalState),
      options_(std::get<config::options::ActiveLayoutConfig>(context.typedOptions)) {
  resetLayouts();

  text_ = new QLabel(labelContainer());
  text_->setProperty("class", QStringLiteral("label"));
  text_->setAlignment(Qt::AlignCenter);
  labelContainer()->layout()->addWidget(text_);

  setCallbackBinding({QString::fromStdString(options_.callbacks.on_left),
                      QString::fromStdString(options_.callbacks.on_middle),
                      QString::fromStdString(options_.callbacks.on_right),
                      QStringLiteral("do_nothing")});
  const auto add = [this](const char* name, std::function<void()> fn) {
    registerCallback(QString::fromLatin1(name), [fn = std::move(fn)](const QStringList&) { fn(); });
  };
  add("next_layout", [this] { nextLayout(); });
  add("prev_layout", [this] { prevLayout(); });
  add("flip_layout", [this] { flip(QStringLiteral("horizontal")); });
  add("flip_layout_horizontal", [this] { flip(QStringLiteral("horizontal")); });
  add("flip_layout_vertical", [this] { flip(QStringLiteral("vertical")); });
  add("flip_layout_horizontal_and_vertical",
      [this] { flip(QStringLiteral("horizontal-and-vertical")); });
  add("first_layout", [this] { firstLayout(); });
  add("toggle_tiling", [this] { toggle(QStringLiteral("tiling")); });
  add("toggle_float", [this] { toggle(QStringLiteral("float")); });
  add("toggle_monocle", [this] { toggle(QStringLiteral("monocle")); });
  add("toggle_maximize", [this] { toggle(QStringLiteral("maximize")); });
  add("toggle_pause", [this] { toggle(QStringLiteral("pause")); });
  add("toggle_layout_menu", [this] { showLayoutMenu(); });

  connect(&bus_, &EventBus::event, this, &KomorebiActiveLayoutWidget::onBusEvent);
  hide();
  if (subscribe) emit bus_.event(komorebi::kSubscribeEvent, {});
}

void KomorebiActiveLayoutWidget::onBusEvent(const QString& name, const QVariantMap& payload) {
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
    qWarning().noquote() << "komorebi active layout update failed:" << error.what();
  }
}

void KomorebiActiveLayoutWidget::resetLayouts() {
  layouts_.clear();
  for (const std::string& layout : options_.layouts) {
    layouts_.push_back(QString::fromStdString(layout).replace(QLatin1Char('_'), QLatin1Char('-')));
  }
}

void KomorebiActiveLayoutWidget::onConnect(JsonPtr state) {
  updateActiveLayout(state, true);
  if (isHidden()) show();
}

void KomorebiActiveLayoutWidget::onUpdate(JsonPtr, JsonPtr state) {
  updateActiveLayout(state, false);
}

void KomorebiActiveLayoutWidget::onDisconnect() {
  if (options_.hide_if_offline) hide();
}

bool KomorebiActiveLayoutWidget::updateState(const JsonPtr& state) {
  std::optional<long long> monitor;
  if (monitorResolver_) {
    monitor = monitorResolver_();
  } else if (QWidget* top = window(); top && top != this) {
    monitor = platform::monitorFromWindow(top->winId());
  }
  state_ = state;
  if (!state || !komorebi::truthy(state.get())) return false;
  if (!monitor) {
    screen_ = nullptr;
    return false;
  }
  int index = 0;
  screen_ = komorebi::screenById(*state, *monitor, &index);
  if (!screen_) return false;
  owner_ = state;
  screenIndex_ = index;
  return true;
}

bool KomorebiActiveLayoutWidget::isPaused() const {
  return komorebi::truthy(child(state_.get(), "is_paused"));
}

bool KomorebiActiveLayoutWidget::workspaceFlag(const char* key) const {
  return komorebi::truthy(child(workspace_, key));
}

std::string KomorebiActiveLayoutWidget::iconFor(const std::string& snakeName) const {
  const auto& icons = options_.layout_icons;
  if (snakeName == "bsp") return icons.bsp;
  if (snakeName == "columns") return icons.columns;
  if (snakeName == "rows") return icons.rows;
  if (snakeName == "grid") return icons.grid;
  if (snakeName == "scrolling") return icons.scrolling;
  if (snakeName == "vertical_stack") return icons.vertical_stack;
  if (snakeName == "horizontal_stack") return icons.horizontal_stack;
  if (snakeName == "ultrawide_vertical_stack") return icons.ultrawide_vertical_stack;
  if (snakeName == "right_main_vertical_stack") return icons.right_main_vertical_stack;
  return "unknown layout";
}

bool KomorebiActiveLayoutWidget::labelInfo(QString* name, QString* icon) const {
  if (isPaused()) {
    *name = QStringLiteral("Paused");
    *icon = QString::fromStdString(options_.layout_icons.paused);
  } else if (!workspaceFlag("tile")) {
    *name = QStringLiteral("Floating");
    *icon = QString::fromStdString(options_.layout_icons.floating);
  } else if (workspaceFlag("maximized_window")) {
    *name = QStringLiteral("Maximized");
    *icon = QString::fromStdString(options_.layout_icons.maximized);
  } else if (workspaceFlag("monocle_container")) {
    *name = QStringLiteral("Monocle");
    *icon = QString::fromStdString(options_.layout_icons.monocle);
  } else {
    // A "Custom" layout has no "Default" entry; Python raises KeyError and keeps the old label.
    const Json* layout = child(child(workspace_, "layout"), "Default");
    if (!layout || !layout->is_string()) return false;
    const std::string layoutName = layout->get<std::string>();
    const auto key = layoutIconKeys().find(layoutName);
    if (key == layoutIconKeys().end()) return false;
    *name = QString::fromStdString(layoutName);
    *icon = QString::fromStdString(iconFor(key->second));
  }
  return true;
}

void KomorebiActiveLayoutWidget::updateActiveLayout(const JsonPtr& state, bool isConnect) {
  if (!updateState(state) || !screen_) return;
  const auto focused = komorebi::focusedWorkspaceIndex(screen_);
  const Json* workspace = focused ? komorebi::workspaceAt(screen_, *focused) : nullptr;
  if (!focused || !komorebi::truthy(workspace)) {
    workspace_ = nullptr;
    return;
  }
  workspace_ = workspace;
  workspaceIndex_ = *focused;

  QString layoutName;
  QString layoutIcon;
  if (!labelInfo(&layoutName, &layoutIcon)) {
    qWarning() << "komorebi active layout: unsupported layout on the focused workspace";
    return;
  }

  if (isConnect) {
    // Align the cycling list with the workspace's layout (unknown layouts count as bsp).
    const Json* layout = child(child(workspace_, "layout"), "Default");
    std::string command = "bsp";
    if (layout && layout->is_string()) {
      const auto found = layoutCommands().find(layout->get<std::string>());
      if (found != layoutCommands().end()) command = found->second;
    }
    const QString wanted = QString::fromStdString(command);
    for (size_t i = 0; i < layouts_.size() && layouts_.front() != wanted; ++i) {
      layouts_.push_front(layouts_.back());
      layouts_.pop_back();
    }
  }

  QString text = QString::fromStdString(options_.label);
  text.replace(QStringLiteral("{icon}"), layoutIcon);
  text.replace(QStringLiteral("{layout_name}"), layoutName);
  text_->setText(text);
  if (text_->isHidden()) show();
}

bool KomorebiActiveLayoutWidget::shiftAllowed() const {
  return !(!workspaceFlag("tile") || workspaceFlag("monocle_container") ||
           workspaceFlag("maximized_window") || isPaused());
}

void KomorebiActiveLayoutWidget::changeLayout(const QString& layout) {
  if (!screen_ || !workspace_) return;
  komorebi::sendCommand(bus_, {QStringLiteral("workspace-layout"), QString::number(screenIndex_),
                               QString::number(workspaceIndex_), layout});
}

void KomorebiActiveLayoutWidget::firstLayout() {
  if (!shiftAllowed()) return;
  resetLayouts();
  if (!layouts_.empty()) changeLayout(layouts_.front());
}

void KomorebiActiveLayoutWidget::nextLayout() {
  if (shiftAllowed()) {
    if (layouts_.empty()) return;
    layouts_.push_front(layouts_.back());  // deque.rotate(1)
    layouts_.pop_back();
    changeLayout(layouts_.front());
  } else {
    toggleBlockingState();
  }
}

void KomorebiActiveLayoutWidget::prevLayout() {
  if (shiftAllowed()) {
    if (layouts_.empty()) return;
    layouts_.push_back(layouts_.front());  // deque.rotate(-1)
    layouts_.pop_front();
    changeLayout(layouts_.front());
  } else {
    toggleBlockingState();
  }
}

void KomorebiActiveLayoutWidget::toggleBlockingState() {
  if (isPaused()) {
    toggle(QStringLiteral("pause"));
  } else if (!workspaceFlag("tile")) {
    toggle(QStringLiteral("tiling"));
  } else if (workspaceFlag("monocle_container")) {
    toggle(QStringLiteral("monocle"));
  } else if (workspaceFlag("maximized_window")) {
    toggle(QStringLiteral("maximize"));
  }
}

void KomorebiActiveLayoutWidget::toggle(const QString& what) {
  // Python runs `focus-monitor-at-cursor && toggle-<what>` through the shell.
  komorebi::sendCommandChain(
      bus_, {{QStringLiteral("focus-monitor-at-cursor")}, {QStringLiteral("toggle-") + what}});
}

void KomorebiActiveLayoutWidget::flip(const QString& direction) {
  komorebi::sendCommand(bus_, {QStringLiteral("flip-layout"), direction});
}

void KomorebiActiveLayoutWidget::showLayoutMenu() {
  const auto& menuOptions = options_.layout_menu;
  if (menu_) menu_->deleteLater();
  auto* menu = new PinnablePopup(this, globalState_, menuOptions.blur, menuOptions.round_corners,
                                 QString::fromStdString(menuOptions.round_corners_type),
                                 QString::fromStdString(menuOptions.border_color));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  menu->setProperty("class", QStringLiteral("komorebi-layout-menu"));
  menu_ = menu;

  auto* mainLayout = new QVBoxLayout(menu);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  const auto addItem = [&](const QString& icon, const QString& text, std::function<void()> action) {
    auto* item = new MenuItem(std::move(action));
    item->setProperty("class", QStringLiteral("menu-item"));
    auto* itemLayout = new QHBoxLayout(item);
    itemLayout->setContentsMargins(0, 0, 0, 0);
    if (menuOptions.show_layout_icons) {
      auto* iconLabel = new QLabel(icon);
      iconLabel->setProperty("class", QStringLiteral("menu-item-icon"));
      iconLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      iconLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
      itemLayout->addWidget(iconLabel);
    }
    auto* textLabel = new QLabel(text);
    textLabel->setProperty("class", QStringLiteral("menu-item-text"));
    textLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    textLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    itemLayout->addWidget(textLabel);
    mainLayout->addWidget(item);
  };

  for (const std::string& layout : options_.layouts) {
    const QString name = QString::fromStdString(layout);
    addItem(QString::fromStdString(iconFor(layout)), titleCase(name), [this, name, menu] {
      changeLayout(QString(name).replace(QLatin1Char('_'), QLatin1Char('-')));
      menu->hide();
    });
  }

  auto* separator = new QFrame(menu);
  separator->setFrameShape(QFrame::HLine);
  separator->setProperty("class", QStringLiteral("separator"));
  separator->setStyleSheet(QStringLiteral("border:none"));
  mainLayout->addWidget(separator);

  const auto addToggle = [&](const QString& label, const std::string& icon, const QString& what) {
    addItem(QString::fromStdString(icon), label, [this, what, menu] {
      toggle(what);
      menu->hide();
    });
  };
  addToggle(QStringLiteral("Toggle Tiling"), options_.layout_icons.tiling,
            QStringLiteral("tiling"));
  addToggle(QStringLiteral("Toggle Monocle"), options_.layout_icons.monocle,
            QStringLiteral("monocle"));
  addToggle(QStringLiteral("Toggle Pause"), options_.layout_icons.paused, QStringLiteral("pause"));

  menu->adjustSize();
  menu->setPosition(QString::fromStdString(menuOptions.alignment),
                    QString::fromStdString(menuOptions.direction), menuOptions.offset_left,
                    menuOptions.offset_top);
  menu->show();
}

}  // namespace yasb::widgets
