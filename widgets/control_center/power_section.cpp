#include <QAction>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>

#include "widgets/control_center/control_center_sections.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }

}  // namespace

PowerSection::PowerSection(QWidget* parent, const config::options::PowerSectionConfig& options,
                           SectionContext context)
    : QFrame(parent), options_(options), context_(std::move(context)) {
  setProperty("class", "section power");
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  planButton_ = new ClickFrame(this);
  planButton_->setProperty("class", "button plan-name");
  connect(planButton_, &ClickFrame::clicked, this, [this] { showPlanMenu(); });

  planLabel_ = new ElidedLabel(planButton_);
  planLabel_->setFullText(QStringLiteral("Unknown"));
  setupButtonLayout(planButton_, qs(options_.power_plan_title), planLabel_,
                    qs(options_.button_menu_icon));

  updatePlansState();

  auto* grid = new QGridLayout;
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(0);
  grid->setVerticalSpacing(0);
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 1);

  if (!context_.system.windows10) {
    // Each button takes one column (50% width).
    grid->addWidget(planButton_, 0, 0);
    buildModeButton(grid);
  } else {
    // The plan button spans both columns (100% width).
    grid->addWidget(planButton_, 0, 0, 1, 2);
  }
  layout->addLayout(grid);
}

PowerSection::~PowerSection() { closeMenus(); }

void PowerSection::setupButtonLayout(QWidget* button, const QString& title, ElidedLabel* label,
                                     const QString& icon) {
  auto* buttonLayout = new QHBoxLayout(button);
  buttonLayout->setContentsMargins(0, 0, 0, 0);
  buttonLayout->setSpacing(0);

  auto* textLayout = new QVBoxLayout;
  textLayout->setContentsMargins(0, 0, 0, 0);
  textLayout->setSpacing(0);

  auto* titleLabel = new QLabel(title, button);
  titleLabel->setProperty("class", "title");
  titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  textLayout->addWidget(titleLabel);

  label->setProperty("class", "subtext");
  label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  textLayout->addWidget(label);

  auto* iconLabel = new QLabel(icon, button);
  iconLabel->setProperty("class", "icon");
  iconLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

  buttonLayout->addLayout(textLayout, 1);
  buttonLayout->addWidget(iconLabel, 0, Qt::AlignRight | Qt::AlignVCenter);
}

QString PowerSection::modeName(const std::optional<platform::PowerMode>& mode) {
  return mode ? mode->name : QStringLiteral("Unknown");
}

QMenu* PowerSection::menu(const QString& key) const {
  const auto it = menus_.find(key);
  return it == menus_.end() ? nullptr : it->second.data();
}

void PowerSection::closeMenus() {
  for (auto& [key, menu] : menus_) {
    if (menu) menu->close();
  }
  menus_.clear();
}

void PowerSection::showMenu(const QString& key, ClickFrame* button,
                            const std::vector<std::pair<QString, QUuid>>& items,
                            const std::function<void(const QUuid&, const QString&)>& onSelect) {
  const QString otherKey =
      key == QLatin1String("plan") ? QStringLiteral("mode") : QStringLiteral("plan");
  if (const auto other = menus_.find(otherKey); other != menus_.end()) {
    if (other->second) other->second->close();
    menus_.erase(other);
  }
  if (const auto own = menus_.find(key); own != menus_.end()) {
    const bool visible = own->second && own->second->isVisible();
    if (visible) own->second->close();
    menus_.erase(own);
    if (visible) return;
  }

  QMenu* popupMenu = createContextMenu(window(), context_.globalState.isDark());
  popupMenu->setMinimumWidth(button->width());
  for (const auto& [label, guid] : items) {
    QAction* action = popupMenu->addAction(label);
    connect(action, &QAction::triggered, this,
            [onSelect, guid = guid, label = label] { onSelect(guid, label); });
  }
  menus_[key] = popupMenu;

  const QPoint pos = button->mapToGlobal(QPoint(0, button->height() + 6));
  button->setAttribute(Qt::WA_UnderMouse, false);
  button->update();
  popupMenu->popup(pos);
}

void PowerSection::showPlanMenu() {
  std::vector<std::pair<QString, QUuid>> items;
  for (const platform::PowerPlan& plan : plans_) items.emplace_back(plan.name, plan.guid);
  showMenu(QStringLiteral("plan"), planButton_, items,
           [this](const QUuid& guid, const QString& name) { applyPlan(guid, name); });
}

void PowerSection::showModeMenu() {
  if (!modeEnabled_ || !modeButton_ || !context_.system.powerModes) return;
  std::vector<std::pair<QString, QUuid>> items;
  for (const platform::PowerMode& mode : context_.system.powerModes())
    items.emplace_back(mode.name, mode.guid);
  showMenu(QStringLiteral("mode"), modeButton_, items,
           [this](const QUuid& guid, const QString& name) { applyMode(guid, name); });
}

void PowerSection::updatePlansState() {
  std::optional<QUuid> active;
  plans_.clear();
  if (context_.system.powerPlans) plans_ = context_.system.powerPlans(&active);
  activePlan_ = active;
  QString activeName = QStringLiteral("Unknown");
  for (const platform::PowerPlan& plan : plans_) {
    if (active && plan.guid == *active) {
      activeName = plan.name;
      break;
    }
  }
  planLabel_->setFullText(activeName);
}

void PowerSection::refreshState() {
  closeMenus();
  updatePlansState();
  if (!context_.system.windows10) refreshModeState();
}

void PowerSection::applyPlan(const QUuid& guid, const QString& name) {
  if (activePlan_ && *activePlan_ == guid) return;
  planLabel_->setFullText(name);
  const unsigned long result =
      context_.system.setPowerPlan ? context_.system.setPowerPlan(guid) : 1;
  if (result == 0) {
    activePlan_ = guid;
    if (!context_.system.windows10) {
      modeEnabled_ = context_.system.powerModeSupported && context_.system.powerModeSupported();
      applyModeButtonState();
    }
  } else {
    qWarning().noquote() << "Failed to activate power plan, error code:" << result;
    refreshState();
  }
}

void PowerSection::buildModeButton(QGridLayout* grid) {
  const auto mode = context_.system.activePowerMode ? context_.system.activePowerMode()
                                                    : std::optional<platform::PowerMode>();
  modeEnabled_ =
      activePlan_ && context_.system.powerModeSupported && context_.system.powerModeSupported();

  modeButton_ = new ClickFrame(this);
  modeButton_->setProperty("class", "button mode-name");
  connect(modeButton_, &ClickFrame::clicked, this, [this] { showModeMenu(); });

  modeLabel_ = new ElidedLabel(modeButton_);
  modeLabel_->setFullText(modeName(mode));
  setupButtonLayout(modeButton_, qs(options_.power_mode_title), modeLabel_,
                    qs(options_.button_menu_icon));

  grid->addWidget(modeButton_, 0, 1);
  applyModeButtonState();
}

void PowerSection::applyModeButtonState() {
  if (!modeButton_) return;
  setCssClass(modeButton_,
              modeEnabled_ ? QStringLiteral("mode-name") : QStringLiteral("mode-name disabled"));
  if (!modeLabel_) return;
  if (!modeEnabled_) {
    modeLabel_->setFullText(QStringLiteral("Automatic"));
  } else {
    modeLabel_->setFullText(modeName(context_.system.activePowerMode
                                         ? context_.system.activePowerMode()
                                         : std::optional<platform::PowerMode>()));
  }
}

void PowerSection::refreshModeState() {
  if (modeLabel_) {
    modeLabel_->setFullText(modeName(context_.system.activePowerMode
                                         ? context_.system.activePowerMode()
                                         : std::optional<platform::PowerMode>()));
  }
  modeEnabled_ = context_.system.powerModeSupported && context_.system.powerModeSupported();
  applyModeButtonState();
}

void PowerSection::applyMode(const QUuid& guid, const QString& name) {
  if (!context_.system.setPowerMode || !context_.system.setPowerMode(guid))
    qWarning().noquote() << "Failed to set power mode to" << name;
  if (modeLabel_) modeLabel_->setFullText(name);
}

}  // namespace yasb::widgets
