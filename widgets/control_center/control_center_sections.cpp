#include "widgets/control_center/control_center_sections.h"

#include <QAction>
#include <QDebug>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

#include "core/widgets/backdrop.h"
#include "core/widgets/tooltip.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }

bool hasId(const std::vector<config::options::ControlCenterActionConfig>& actions, const char* id) {
  return std::any_of(actions.begin(), actions.end(),
                     [id](const auto& action) { return action.id == id; });
}

}  // namespace

void setCssClass(QWidget* widget, const QString& cssClass) {
  if (widget->property("class").toString() == cssClass) return;
  widget->setProperty("class", cssClass);
  bar::refreshWidgetStyle(widget);
}

QMenu* createContextMenu(QWidget* parent, bool dark) {
  auto* menu = new QMenu(parent);
  menu->setAttribute(Qt::WA_DeleteOnClose);
  // apply_qmenu_style: Fusion removes the menu shadow; blur and round corners on show.
  if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
    fusion->setParent(menu);
    menu->setStyle(fusion);
  }
  QObject::connect(menu, &QMenu::aboutToShow, menu, [menu] {
    try {
      enableBlur(menu->winId(), false, true, QStringLiteral("normal"), QStringLiteral("None"));
    } catch (...) {
    }
  });
  menu->setProperty("class",
                    dark ? QStringLiteral("context-menu dark") : QStringLiteral("context-menu"));
  return menu;
}

// --- ClickFrame ---------------------------------------------------------------------------------

void ClickFrame::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    pressed_ = true;
    event->accept();
    return;
  }
  QFrame::mousePressEvent(event);
}

void ClickFrame::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const bool inside = rect().contains(event->position().toPoint());
    const bool wasPressed = pressed_;
    pressed_ = false;
    event->accept();
    if (wasPressed && inside) emit clicked();
    return;
  }
  QFrame::mouseReleaseEvent(event);
}

// --- SystemControlsSection ----------------------------------------------------------------------

SystemControlsSection::SystemControlsSection(QWidget* parent,
                                             const config::options::SystemControlsConfig& options,
                                             SectionContext context)
    : QFrame(parent), options_(options), context_(std::move(context)) {
  setProperty("class", "section system-controls");
  auto* layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  profileButton_ = new QPushButton(this);
  profileButton_->setProperty("class", "button profile-image");
  updateProfileImage();
  connect(profileButton_, &QPushButton::clicked, this, [this] { showProfileMenu(); });
  if (context_.tooltip && context_.system.userName) {
    setTooltip(profileButton_, context_.system.userName(), context_.globalState, 400,
               QStringLiteral("top"));
  }
  layout->addWidget(profileButton_);

  layout->addStretch(1);

  settingsButton_ = new QPushButton(qs(options_.settings_icon), this);
  settingsButton_->setProperty("class", "button settings");
  connect(settingsButton_, &QPushButton::clicked, this, [this] { showSettingsMenu(); });
  if (context_.tooltip) {
    setTooltip(settingsButton_, QStringLiteral("Settings"), context_.globalState, 400,
               QStringLiteral("top"));
  }
  layout->addWidget(settingsButton_);

  powerButton_ = new QPushButton(qs(options_.power_icon), this);
  powerButton_->setProperty("class", "button power");
  connect(powerButton_, &QPushButton::clicked, this, [this] { showPowerMenu(); });
  if (context_.tooltip) {
    setTooltip(powerButton_, QStringLiteral("Power"), context_.globalState, 400,
               QStringLiteral("top"));
  }
  layout->addWidget(powerButton_);
}

bool SystemControlsSection::event(QEvent* event) {
  // Built before the popup has a screen, so the scale is only known once it is shown.
  if (event->type() == QEvent::Show || event->type() == QEvent::DevicePixelRatioChange)
    updateProfileImage();
  return QFrame::event(event);
}

void SystemControlsSection::updateProfileImage() {
  const qreal dpr = devicePixelRatioF();
  if (dpr == profileDpr_ || !profileButton_) return;
  profileDpr_ = dpr;
  const int size = options_.profile_image_size;
  const QPixmap pixmap = buildProfileImage(size, dpr);
  if (pixmap.isNull()) return;
  profileButton_->setIcon(QIcon(pixmap));
  profileButton_->setIconSize(QSize(size, size));
}

QPixmap SystemControlsSection::buildProfileImage(int size, qreal dpr) const {
  const QString path = context_.system.avatarPath ? context_.system.avatarPath() : QString();
  if (path.isEmpty()) return {};
  QImage image(path);
  if (image.isNull()) return {};
  image = image.convertToFormat(QImage::Format_RGBA8888);
  const int side = std::min(image.width(), image.height());
  const QImage square =
      image.copy((image.width() - side) / 2, (image.height() - side) / 2, side, side);
  const int physical = std::max(1, static_cast<int>(std::lround(size * dpr)));
  const QImage resized =
      square.scaled(physical, physical, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  QPixmap rounded(physical, physical);
  rounded.fill(Qt::transparent);
  QPainter painter(&rounded);
  painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath clip;
  clip.addEllipse(0, 0, physical, physical);
  painter.setClipPath(clip);
  painter.drawImage(0, 0, resized);
  painter.end();
  rounded.setDevicePixelRatio(dpr);
  return rounded;
}

void SystemControlsSection::showMenu(QPointer<QMenu>& slot, QPushButton* button, bool rightAlign,
                                     int gap, const std::function<void(QMenu*)>& fill) {
  if (slot && slot->isVisible()) {
    slot->close();
    return;
  }
  if (!button) return;
  QMenu* menu = createContextMenu(window(), context_.globalState.isDark());
  fill(menu);
  slot = menu;
  const int x = rightAlign ? button->width() - menu->sizeHint().width() : 0;
  const QPoint pos = button->mapToGlobal(QPoint(x, button->height() + gap));
  button->setAttribute(Qt::WA_UnderMouse, false);
  button->update();
  menu->popup(pos);
}

void SystemControlsSection::runPowerAction(const std::function<void()>& action) {
  if (context_.dismissPopup) context_.dismissPopup();
  if (action) action();
}

void SystemControlsSection::showSettingsMenu() {
  showMenu(settingsMenu_, settingsButton_, true, 6, [this](QMenu* menu) {
    struct Link {
      const char* label;
      const char* uri;
    };
    static const Link links[] = {
        {"Windows Update", "ms-settings:windowsupdate"},
        {"Network && Internet", "ms-settings:network"},
        {"Bluetooth && Devices", "ms-settings:bluetooth"},
        {"Sound", "ms-settings:sound"},
        {"Notifications", "ms-settings:notifications"},
        {"Power && Battery", "ms-settings:powersleep"},
        {"Apps", "ms-settings:appsfeatures"},
        {"Accounts", "ms-settings:yourinfo"},
        {"Privacy && Security", "ms-settings:privacy"},
        {nullptr, nullptr},
        {"All Settings", "ms-settings:"},
    };
    for (const Link& link : links) {
      if (!link.label) {
        menu->addSeparator();
        continue;
      }
      const QString uri = QString::fromLatin1(link.uri);
      QAction* action = menu->addAction(QString::fromLatin1(link.label));
      connect(action, &QAction::triggered, this, [this, uri] {
        if (context_.system.openLink) context_.system.openLink(uri);
      });
    }
  });
}

void SystemControlsSection::showProfileMenu() {
  showMenu(profileMenu_, profileButton_, false, 6, [this](QMenu* menu) {
    const QString name = context_.system.userName ? context_.system.userName() : QString();
    QAction* user = menu->addAction(name);
    connect(user, &QAction::triggered, this, [this] {
      if (context_.system.openLink)
        context_.system.openLink(QStringLiteral("ms-settings:yourinfo"));
    });
    menu->addSeparator();
    connect(menu->addAction(QStringLiteral("Lock")), &QAction::triggered, this,
            [this] { runPowerAction(context_.system.lock); });
    connect(menu->addAction(QStringLiteral("Sign Out")), &QAction::triggered, this,
            [this] { runPowerAction(context_.system.signOut); });
  });
}

void SystemControlsSection::showPowerMenu() {
  showMenu(powerMenu_, powerButton_, true, 6, [this](QMenu* menu) {
    connect(menu->addAction(QStringLiteral("Sleep")), &QAction::triggered, this,
            [this] { runPowerAction(context_.system.sleep); });
    connect(menu->addAction(QStringLiteral("Hibernate")), &QAction::triggered, this,
            [this] { runPowerAction(context_.system.hibernate); });
    connect(menu->addAction(QStringLiteral("Restart")), &QAction::triggered, this,
            [this] { runPowerAction(context_.system.restart); });
    connect(menu->addAction(QStringLiteral("Shut Down")), &QAction::triggered, this,
            [this] { runPowerAction(context_.system.shutdown); });
  });
}

void SystemControlsSection::refreshState() {
  for (const QPointer<QMenu>& menu : {settingsMenu_, profileMenu_, powerMenu_}) {
    if (menu) menu->close();
  }
}

// --- QuickActionsSection ------------------------------------------------------------------------

QuickActionsSection::QuickActionsSection(QWidget* parent,
                                         const config::options::QuickActionsSectionConfig& options,
                                         SectionContext context)
    : QFrame(parent), options_(options), context_(std::move(context)) {
  setProperty("class", "section quick-actions");
  const auto& system = context_.system;
  if (hasId(options_.actions, "toggle_theme") && system.themeMode) theme_ = system.themeMode();
  if ((hasId(options_.actions, "toggle_dnd") || hasId(options_.actions, "cycle_dnd")) &&
      system.dndStatus) {
    dnd_ = system.dndStatus();
  }

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* grid = new QGridLayout;
  grid->setContentsMargins(0, 0, 0, 0);
  grid->setHorizontalSpacing(0);
  grid->setVerticalSpacing(0);

  const int columns = std::max(1, options_.columns);
  for (int i = 0; i < columns; ++i) grid->setColumnStretch(i, 1);
  int index = 0;
  for (const auto& action : options_.actions) {
    ClickFrame* button = createButton(action);
    buttons_[action.id] = button;
    grid->addWidget(button, index / columns, index % columns);
    ++index;
  }
  layout->addLayout(grid);
}

ClickFrame* QuickActionsSection::createButton(
    const config::options::ControlCenterActionConfig& action) {
  auto* button = new ClickFrame(this);
  button->setMouseTracking(true);
  button->setProperty("class", QStringLiteral("button ") + qs(action.id));
  connect(button, &ClickFrame::clicked, this, [this, action] { runAction(action); });

  const bool inline_ = options_.label_position == "inline";
  const Qt::Alignment align =
      inline_ ? Qt::AlignLeft | Qt::AlignVCenter : Qt::Alignment(Qt::AlignCenter);
  QBoxLayout* layout = inline_ ? static_cast<QBoxLayout*>(new QHBoxLayout(button))
                               : static_cast<QBoxLayout*>(new QVBoxLayout(button));
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  if (inline_) layout->setAlignment(align);

  auto* icon = new QLabel(qs(action.icon), button);
  icon->setProperty("class", "icon");
  icon->setAlignment(align);

  auto* text = new ElidedLabel(button);
  text->setFullText(qs(action.label));
  text->setProperty("class", "title");
  text->setAlignment(align);

  if (inline_) {
    layout->addWidget(icon, 0, align);
    layout->addWidget(text, 0, align);
  } else {
    layout->addWidget(icon);
    layout->addWidget(text);
  }
  if (context_.tooltip) {
    setTooltip(button, qs(action.label), context_.globalState, 400, QStringLiteral("top"));
  }
  return button;
}

ClickFrame* QuickActionsSection::button(const QString& id) const {
  const auto it = buttons_.find(id.toStdString());
  return it == buttons_.end() ? nullptr : it->second;
}

void QuickActionsSection::runAction(const config::options::ControlCenterActionConfig& action) {
  const auto& system = context_.system;
  if (action.command && !action.command->empty()) {
    // command.split(None, 1): the first word is the file, the rest its parameters.
    const QString command = qs(*action.command);
    const QString trimmed = command.trimmed();
    if (!trimmed.isEmpty()) {
      qsizetype split = 0;
      while (split < trimmed.size() && !trimmed.at(split).isSpace()) ++split;
      qsizetype rest = split;
      while (rest < trimmed.size() && trimmed.at(rest).isSpace()) ++rest;
      if (system.runCommand) {
        system.runCommand(trimmed.left(split),
                          rest < trimmed.size() ? trimmed.mid(rest) : QString());
      }
    }
    if (context_.refreshPopup) context_.refreshPopup();
    return;
  }

  const std::string& id = action.id;
  if (id == "toggle_theme") {
    if (system.toggleTheme) {
      if (const auto mode = system.toggleTheme()) theme_ = *mode;
    }
  } else if (id == "toggle_dnd") {
    if (system.dndStatus && system.setDndStatus) {
      const QString status = system.dndStatus();
      const QString next = status != QLatin1String("disabled") ? QStringLiteral("disabled")
                                                               : QStringLiteral("priority");
      system.setDndStatus(next);
      dnd_ = next;
    }
  } else if (id == "cycle_dnd") {
    if (system.dndStatus && system.setDndStatus) {
      const QString current = system.dndStatus();
      const QString next = current == QLatin1String("disabled")   ? QStringLiteral("priority")
                           : current == QLatin1String("priority") ? QStringLiteral("alarms")
                                                                  : QStringLiteral("disabled");
      system.setDndStatus(next);
      dnd_ = next;
    }
  } else if (id == "screenshot") {
    CustomToolTip::hideActive();
    if (context_.dismissPopup) context_.dismissPopup();
    if (system.startScreenshot) system.startScreenshot();
    return;
  } else if (id == "touch_keyboard") {
    if (system.toggleTouchKeyboard) system.toggleTouchKeyboard();
  } else if (id == "toggle_mute") {
    if (system.output && system.output->available()) {
      system.output->setMuted(!system.output->muted().value_or(false));
    }
  } else if (id == "toggle_mic_mute") {
    if (system.input && system.input->available()) {
      system.input->setMuted(!system.input->muted().value_or(false));
    }
  } else {
    qDebug().noquote() << "Unknown control center action:" << QString::fromStdString(id);
  }
  if (context_.refreshPopup) context_.refreshPopup();
}

bool QuickActionsSection::isActive(const std::string& id) const {
  const auto& system = context_.system;
  if (id == "toggle_dnd" || id == "cycle_dnd") return dnd_ && *dnd_ != QLatin1String("disabled");
  if (id == "toggle_theme") return theme_ && *theme_ == QLatin1String("dark");
  if (id == "toggle_mute")
    return system.output && system.output->available() && system.output->muted().value_or(false);
  if (id == "toggle_mic_mute")
    return system.input && system.input->available() && system.input->muted().value_or(false);
  return false;
}

bool QuickActionsSection::isDisabled(const std::string& id) const {
  const auto& system = context_.system;
  if (id == "toggle_mute") return !system.output || !system.output->available();
  if (id == "toggle_mic_mute") return !system.input || !system.input->available();
  return false;
}

void QuickActionsSection::refreshState() {
  const auto& system = context_.system;
  if (theme_ && system.themeMode) theme_ = system.themeMode();
  if (dnd_ && system.dndStatus) dnd_ = system.dndStatus();

  for (const auto& [id, button] : buttons_) {
    const bool active = isActive(id);
    const bool disabled = isDisabled(id);

    QString classes = QStringLiteral("button ") + qs(id);
    if (active) classes += QStringLiteral(" active");
    if (disabled) classes += QStringLiteral(" disabled");
    setCssClass(button, classes);
    button->setEnabled(!disabled);

    if (context_.tooltip) {
      const auto it = std::find_if(options_.actions.begin(), options_.actions.end(),
                                   [&](const auto& action) { return action.id == id; });
      const QString label = it != options_.actions.end() ? qs(it->label) : qs(id);
      setTooltip(button, disabled ? label + QStringLiteral(" (Disabled)") : label,
                 context_.globalState, 400, QStringLiteral("top"));
    }

    for (QWidget* child : button->findChildren<QWidget*>()) {
      QStringList childClasses =
          child->property("class").toString().split(QLatin1Char(' '), Qt::SkipEmptyParts);
      childClasses.removeAll(QStringLiteral("active"));
      childClasses.removeAll(QStringLiteral("disabled"));
      if (active) childClasses << QStringLiteral("active");
      if (disabled) childClasses << QStringLiteral("disabled");
      setCssClass(child, childClasses.join(QLatin1Char(' ')));
    }
  }
}

}  // namespace yasb::widgets
