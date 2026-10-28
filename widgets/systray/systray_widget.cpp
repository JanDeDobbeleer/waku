#include "widgets/systray/systray_widget.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QMenu>
#include <QScreen>
#include <QStyle>
#include <QStyleFactory>
#include <algorithm>

#include "core/widgets/backdrop.h"
#include "core/widgets/polling_worker_registry.h"
#include "widgets/system_widget_helpers.h"
#include "widgets/systray/icon_scale.h"
#include "widgets/systray/systray_service.h"

namespace yasb::widgets {
namespace {

QString qs(const std::string& text) { return QString::fromStdString(text); }

void repolish(QWidget* widget) {
  if (QStyle* style = widget->style()) {
    style->unpolish(widget);
    style->polish(widget);
  }
}

Qt::KeyboardModifier pinModifier(const std::string& name) {
  const QString lower = qs(name).toLower();
  if (lower == QLatin1String("ctrl")) return Qt::ControlModifier;
  if (lower == QLatin1String("shift")) return Qt::ShiftModifier;
  return Qt::AltModifier;  // "alt" and anything unknown
}

// The 32x32 image Python builds in validate_icon_data (any other size is resized to 32x32 with
// Lanczos plus SHARPEN).
QImage imageFromData(const platform::TrayIconData& tray) {
  if (tray.iconRgba.isEmpty() || tray.iconWidth <= 0 || tray.iconHeight <= 0) return {};
  if (tray.iconRgba.size() < static_cast<qsizetype>(tray.iconWidth) * tray.iconHeight * 4)
    return {};
  QImage image(reinterpret_cast<const uchar*>(tray.iconRgba.constData()), tray.iconWidth,
               tray.iconHeight, tray.iconWidth * 4, QImage::Format_RGBA8888);
  image = image.copy();
  return scaleTrayIcon(image, 32);
}

SystrayDeps acquireDeps(const WidgetContext& context,
                        const config::options::SystrayWidgetConfig& options) {
  SystrayDeps deps;
  auto* service = context.pollingWorkers.service<SystrayService>("systray", [&options] {
    // The offscreen platform is what tests and --screenshot run on: they must never inject into
    // Explorer or put a window in front of the real tray, so no host is created there.
    const bool offscreen = QGuiApplication::platformName() == QLatin1String("offscreen");
    if (offscreen) qInfo() << "Systray: offscreen platform, tray interception disabled";
    return new SystrayService(options.use_hook, QCoreApplication::applicationDirPath(),
                              offscreen ? TrayHostFactories{} : realTrayHostFactories());
  });
  deps.host = service->host();
  deps.windowApi = service->windowApi();
  deps.connectRefresh = service->claimRefreshSignal();
  return deps;
}

}  // namespace

SystrayWidget::SystrayWidget(const WidgetContext& context, std::optional<SystrayDeps> deps)
    : BaseWidget(
          qs(context.name), context.eventBus,
          qs(std::get<config::options::SystrayWidgetConfig>(context.typedOptions).class_name), 0),
      options_(std::get<config::options::SystrayWidgetConfig>(context.typedOptions)),
      deps_(deps ? *deps : acquireDeps(context, options_)),
      globalState_(&context.globalState),
      iconCheckTimer_(this),
      refreshTimer_(this),
      sortTimer_(this),
      pinnedVisibilityTimer_(this) {
  QSet<QUuid> filteredGuids;
  if (!options_.show_battery) filteredGuids.insert(platform::kTrayBatteryIconGuid);
  if (!options_.show_volume) filteredGuids.insert(platform::kTrayVolumeIconGuid);
  if (!options_.show_network) filteredGuids.insert(platform::kTrayNetworkIconGuid);
  QStringList hiddenIcons;
  for (const std::string& name : options_.hide_icons) hiddenIcons << qs(name);
  registry_.setFilter(std::move(filteredGuids), hiddenIcons);

  iconSettings_.iconSize = options_.icon_size;
  iconSettings_.enableTooltips = options_.tooltip;
  iconSettings_.pinModifier = pinModifier(options_.pin_click_modifier);

  // Checks that icons still have a live owner window.
  connect(&iconCheckTimer_, &QTimer::timeout, this, [this] { checkIcons(); });
  iconCheckTimer_.start(5000);

  refreshTimer_.setInterval(200);
  refreshTimer_.setSingleShot(true);
  connect(&refreshTimer_, &QTimer::timeout, this, [this] { refreshSystray(); });

  sortTimer_.setSingleShot(true);
  connect(&sortTimer_, &QTimer::timeout, this, [this] { sortIcons(); });

  pinnedVisibilityTimer_.setSingleShot(true);
  connect(&pinnedVisibilityTimer_, &QTimer::timeout, this,
          [this] { updatePinnedWidgetVisibility(); });

  // BaseWidget's container is the widget-container frame (Python builds its own, same class).
  QFrame* container = labelContainer();
  auto* containerLayout = qobject_cast<QBoxLayout*>(container->layout());

  visibilityButton_ = new QPushButton(this);
  visibilityButton_->setCheckable(true);
  connect(visibilityButton_, &QPushButton::clicked, this,
          [this] { toggleUnpinnedWidgetVisibility(); });
  visibilityButton_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(visibilityButton_, &QPushButton::customContextMenuRequested, this,
          [this](const QPoint& position) { showContextMenu(position); });

  pinnedWidget_ = new TrayDropWidget(this);
  pinnedWidget_->setMinimumWidth(16);
  pinnedWidget_->setProperty("class", QStringLiteral("pinned-container"));
  pinnedWidget_->setProperty("forceshow", false);
  visibilityButton_->setProperty("class", QStringLiteral("unpinned-visibility-btn"));
  connect(pinnedWidget_, &TrayDropWidget::dragStarted, this,
          [this](int width) { onDragStarted(width, true); });
  connect(pinnedWidget_, &TrayDropWidget::dragEnded, this, [this] { onDragEnded(); });

  const QString labelCollapsed = qs(options_.label_collapsed);
  const bool labelLeft = options_.label_position == "left";

  if (options_.show_in_popup) {
    // Popup mode: the unpinned icons live in a grid in a popup window.
    SystrayPopupOptions popupOptions;
    popupOptions.blur = options_.popup.blur;
    popupOptions.roundCorners = options_.popup.round_corners;
    popupOptions.roundCornersType = qs(options_.popup.round_corners_type);
    popupOptions.borderColor = qs(options_.popup.border_color);
    popupOptions.alignment = qs(options_.popup.alignment);
    popupOptions.direction = qs(options_.popup.direction);
    popupOptions.offsetTop = options_.popup.offset_top;
    popupOptions.offsetLeft = options_.popup.offset_left;
    popup_ = new SystrayPopup(this, visibilityButton_, popupOptions, options_.icons_per_row,
                              labelCollapsed);
    unpinnedLayout_ = popup_->gridWidget()->mainLayout();
    connect(popup_->gridWidget(), &TrayDropWidget::dragStarted, this,
            [this](int width) { onDragStarted(width, false); });
    connect(popup_->gridWidget(), &TrayDropWidget::dragEnded, this, [this] { onDragEnded(); });

    // Only the pinned container and the button go in the bar.
    containerLayout->addWidget(pinnedWidget_);
    if (labelLeft)
      containerLayout->insertWidget(0, visibilityButton_);
    else
      containerLayout->insertWidget(-1, visibilityButton_);
    visibilityButton_->setVisible(true);
  } else {
    // Inline mode.
    unpinnedWidget_ = new TrayDropWidget(this);
    unpinnedWidget_->setProperty("class", QStringLiteral("unpinned-container"));
    unpinnedLayout_ = unpinnedWidget_->mainLayout();
    connect(unpinnedWidget_, &TrayDropWidget::dragStarted, this,
            [this](int width) { onDragStarted(width, false); });
    connect(unpinnedWidget_, &TrayDropWidget::dragEnded, this, [this] { onDragEnded(); });

    containerLayout->addWidget(unpinnedWidget_);
    containerLayout->addWidget(pinnedWidget_);
    if (labelLeft)
      containerLayout->insertWidget(0, visibilityButton_);
    else
      containerLayout->insertWidget(-1, visibilityButton_);
    visibilityButton_->setVisible(options_.show_unpinned_button);
  }

  QTimer::singleShot(0, this, [this] { loadState(); });
  if (options_.use_hook) {
    setupClient();
    setContainersVisibility();
  } else {
    QTimer::singleShot(0, this, [this] { setupClient(); });
    QTimer::singleShot(0, this, [this] { setContainersVisibility(); });
  }
}

SystrayWidget::~SystrayWidget() = default;

void SystrayWidget::showContextMenu(const QPoint& position) {
  try {
    auto* menu = new QMenu(window());
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
    menu->setProperty("class", QStringLiteral("context-menu"));
    menu->setContentsMargins(0, 0, 0, 0);
    menu->setLayoutDirection(Qt::LeftToRight);
    QAction* refresh = menu->addAction(QStringLiteral("Refresh Systray"));
    connect(refresh, &QAction::triggered, this, [this] { refreshSystray(); });
    menu->popup(visibilityButton_->mapToGlobal(position));
    menu->activateWindow();
  } catch (const std::exception& error) {
    qWarning().noquote() << "Systray context menu failed:" << error.what();
  }
}

void SystrayWidget::refreshSystray() {
  try {
    // The Python widget suppresses AppBarManager around the broadcast so its own TaskbarCreated
    // does not re-register the app bar. The native bar has no TaskbarCreated handler, so there is
    // nothing to suppress.
    if (deps_.windowApi) deps_.windowApi->broadcastTaskbarCreated();
    qDebug() << "Systray icons refreshed";
  } catch (const std::exception& error) {
    qWarning().noquote() << "Systray refresh failed:" << error.what();
  }
}

void SystrayWidget::setupClient() {
  if (!deps_.host) return;
  platform::TrayHost* host = deps_.host;
  // Tray notifications arrive from the host's worker thread; the connection is queued because
  // this widget lives on the GUI thread.
  connect(host, &platform::TrayHost::iconModified, this,
          [this](const platform::TrayIconData& tray) {
            try {
              onIconModified(tray);
            } catch (const std::exception& error) {
              qWarning().noquote() << "Systray icon update failed:" << error.what();
            }
          });
  connect(host, &platform::TrayHost::iconDeleted, this, [this](const platform::TrayIconData& tray) {
    try {
      onIconDeleted(tray);
    } catch (const std::exception& error) {
      qWarning().noquote() << "Systray icon removal failed:" << error.what();
    }
  });
  if (deps_.connectRefresh) {
    connect(host, &platform::TrayHost::updateIcons, this, [this] { refreshTimer_.start(); });
  }
  if (QCoreApplication::instance()) {
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
      try {
        saveState();
      } catch (const std::exception& error) {
        qWarning().noquote() << "Systray state could not be saved:" << error.what();
      }
    });
  }
  host->start();
}

void SystrayWidget::setContainersVisibility() {
  if (options_.show_in_popup) {
    visibilityButton_->setChecked(false);
    visibilityButton_->setText(qs(options_.label_collapsed));
    return;
  }
  visibilityButton_->setChecked(options_.show_unpinned);
  visibilityButton_->setText(
      qs(options_.show_unpinned ? options_.label_expanded : options_.label_collapsed));
  unpinnedWidget_->setVisible(options_.show_unpinned || !options_.show_unpinned_button);
}

void SystrayWidget::onDragStarted(int iconWidth, bool fromPinned) {
  if (options_.show_in_popup && fromPinned && !popup_->isOpen())
    // Keep the unpinned grid available as a drop target while dragging from the pinned bar.
    popup_->open(qs(options_.label_expanded));
  if (options_.show_in_popup && fromPinned) popup_->gridWidget()->setDropTargetStyle(true);

  // The pinned container always shows during a drag.
  updatePinnedWidgetVisibility(true);
  // An empty pinned container grows to one icon button so it can be dropped on.
  if (isLayoutEmpty(pinnedLayout())) {
    if (iconWidth > 0) pinnedWidget_->setMinimumWidth(iconWidth);
    pinnedWidget_->setDropTargetStyle(true);
  }
}

void SystrayWidget::onDragEnded() {
  if (options_.show_in_popup) popup_->gridWidget()->setDropTargetStyle(false);
  pinnedWidget_->setDropTargetStyle(false);
  pinnedWidget_->setMinimumWidth(16);
  updatePinnedWidgetVisibility();
}

void SystrayWidget::applyIconData(TrayIconButton* icon, const platform::TrayIconData& merged) {
  icon->info = merged;
  icon->hasData = true;
  icon->setIconImage(imageFromData(icon->info));
}

TrayIconButton* SystrayWidget::buttonFor(TrayIconRegistry::Id id) const {
  for (const auto& icon : icons_) {
    if (icon && icon->registryId == id) return icon;
  }
  return nullptr;
}

void SystrayWidget::onIconModified(const platform::TrayIconData& tray) {
  const TrayIconRegistry::Result change = registry_.modified(tray);
  if (change.change == TrayIconRegistry::Change::Ignored) return;

  TrayIconButton* icon =
      change.change == TrayIconRegistry::Change::Added ? nullptr : buttonFor(change.id);
  if (!icon) {
    icon = new TrayIconButton(iconSettings_, deps_.windowApi, globalState_);
    icon->hasData = true;
    icon->registryId = change.id;
    connect(icon, &TrayIconButton::pinnedChanged, this,
            [this](TrayIconButton* button) { onIconPinnedChanged(button); });
    connect(icon, &TrayIconButton::iconMoved, this,
            [this](TrayIconButton* button) { onIconMoved(button); });
    icons_.push_back(icon);

    // The saved pin state and position are looked up by guid, then by executable path.
    const IconState state = savedStateFor(currentState_, tray);

    // Place the new icon in the right container.
    icon->isPinned = state.isPinned;
    if (state.isPinned)
      pinnedWidget_->rowLayout()->addWidget(icon);
    else
      addIconToUnpinned(icon);

    // Once no more icons are arriving, sort them in one go.
    sortTimer_.start(1000);
  }
  applyIconData(icon, *registry_.data(change.id));
  icon->updateIcon();
  const bool wasHidden = icon->isHidden();
  icon->setHidden((tray.uFlags & platform::kNifState) != 0 && tray.dwState == 1);
  if (options_.show_in_popup && wasHidden != icon->isHidden()) relayoutPopupGrid();
  pinnedVisibilityTimer_.start(300);
}

void SystrayWidget::onIconDeleted(const platform::TrayIconData& tray) {
  const std::optional<TrayIconRegistry::Id> id = registry_.deleted(tray);
  if (!id) return;
  TrayIconButton* icon = buttonFor(*id);
  if (!icon) return;
  icons_.erase(std::remove(icons_.begin(), icons_.end(), icon), icons_.end());
  icon->hide();
  icon->deleteLater();
  if (options_.show_in_popup) relayoutPopupGrid();
  pinnedVisibilityTimer_.start(300);
}

void SystrayWidget::removeFromLayout(TrayIconButton* icon) {
  // QLayout::addWidget on a widget that sits in another layout would warn; take it out first.
  if (QWidget* parent = icon->parentWidget(); parent && parent->layout())
    parent->layout()->removeWidget(icon);
}

void SystrayWidget::onIconPinnedChanged(TrayIconButton* icon) {
  removeFromLayout(icon);
  if (!icon->isPinned) {
    pinnedWidget_->rowLayout()->addWidget(icon);
    icon->isPinned = true;
  } else {
    addIconToUnpinned(icon);
    icon->isPinned = false;
  }
  icon->show();
  if (options_.show_in_popup) relayoutPopupGrid();
  pinnedWidget_->refreshStyles();
  saveState();
  updatePinnedWidgetVisibility();
}

void SystrayWidget::onIconMoved(TrayIconButton* icon) {
  icon->isPinned = pinnedLayout()->indexOf(icon) != -1;
  if (options_.show_in_popup) relayoutPopupGrid();
  pinnedWidget_->refreshStyles();
  saveState();
}

void SystrayWidget::checkIcons() {
  if (!deps_.windowApi) return;
  bool changed = false;
  for (const TrayIconRegistry::Id id : registry_.sweepStale(*deps_.windowApi)) {
    TrayIconButton* icon = buttonFor(id);
    if (!icon) continue;
    icons_.erase(std::remove(icons_.begin(), icons_.end(), icon), icons_.end());
    icon->hide();
    icon->deleteLater();
    changed = true;
  }
  if (changed) {
    if (options_.show_in_popup) relayoutPopupGrid();
    pinnedVisibilityTimer_.start(300);
  }
}

bool SystrayWidget::isLayoutEmpty(QLayout* layout) {
  for (int i = 0; i < layout->count(); ++i) {
    QLayoutItem* item = layout->itemAt(i);
    QWidget* widget = item ? item->widget() : nullptr;
    if (widget && !widget->isHidden()) return false;
  }
  return true;
}

void SystrayWidget::updatePinnedWidgetVisibility(bool forceShow) {
  const bool isEmpty = isLayoutEmpty(pinnedLayout());
  pinnedWidget_->setVisible(!isEmpty || forceShow);
  if (forceShow && isEmpty) {
    pinnedWidget_->setProperty("forceshow", true);
    repolish(pinnedWidget_);
  } else if (pinnedWidget_->property("forceshow").toBool() && !isEmpty) {
    pinnedWidget_->setProperty("forceshow", false);
    repolish(pinnedWidget_);
  }
}

void SystrayWidget::toggleUnpinnedWidgetVisibility() {
  if (options_.show_in_popup) {
    popup_->toggle(qs(options_.label_expanded));
    return;
  }
  if (visibilityButton_->isChecked()) {
    unpinnedWidget_->setVisible(true);
    visibilityButton_->setText(qs(options_.label_expanded));
  } else {
    unpinnedWidget_->setVisible(false);
    visibilityButton_->setText(qs(options_.label_collapsed));
  }
}

std::vector<TrayIconButton*> SystrayWidget::iconsInLayout(QLayout* layout) const {
  std::vector<TrayIconButton*> result;
  for (int i = 0; i < layout->count(); ++i) {
    QLayoutItem* item = layout->itemAt(i);
    if (auto* icon = item ? qobject_cast<TrayIconButton*>(item->widget()) : nullptr)
      result.push_back(icon);
  }
  return result;
}

void SystrayWidget::sortIcons() {
  std::vector<TrayIconButton*> unpinned = iconsInLayout(unpinnedLayout_);
  std::vector<TrayIconButton*> pinned = iconsInLayout(pinnedLayout());

  const auto sortIndex = [this](const TrayIconButton* widget) {
    if (!widget->hasData) return 9999;
    const IconState* state =
        currentState_.find(widget->info.guid ? widget->info.guid->toString(QUuid::WithoutBraces)
                                             : widget->info.exePath);
    return state ? state->index : 9999;
  };
  const auto byIndex = [&](const TrayIconButton* a, const TrayIconButton* b) {
    return sortIndex(a) < sortIndex(b);
  };
  std::stable_sort(unpinned.begin(), unpinned.end(), byIndex);
  std::stable_sort(pinned.begin(), pinned.end(), byIndex);

  if (options_.show_in_popup) {
    popup_->sortUnpinned(unpinned);
  } else if (auto* row = unpinnedWidget_->rowLayout()) {
    for (size_t index = 0; index < unpinned.size(); ++index)
      row->insertWidget(static_cast<int>(index), unpinned[index]);
  }
  if (auto* row = pinnedWidget_->rowLayout()) {
    for (size_t index = 0; index < pinned.size(); ++index)
      row->insertWidget(static_cast<int>(index), pinned[index]);
  }
}

QStringList SystrayWidget::visibleStateKeys(QLayout* layout) const {
  QStringList keys;
  for (const TrayIconButton* icon : iconsInLayout(layout)) {
    if (!icon->hasData || icon->isHidden()) continue;
    const QString key = iconStateKey(icon->info);
    if (!keys.contains(key)) keys.append(key);
  }
  return keys;
}

void SystrayWidget::updateCurrentState() {
  yasb::widgets::updateCurrentState(currentState_, visibleStateKeys(pinnedLayout()),
                                    visibleStateKeys(unpinnedLayout_));
}

QString SystrayWidget::screenId() {
  if (screenId_) return *screenId_;
  if (deps_.screenId) {
    screenId_ = deps_.screenId;
  } else if (const QScreen* screen = this->screen()) {
    screenId_ = cleanScreenId(screen->manufacturer(), screen->name(), screen->serialNumber());
  }
  return screenId_ ? *screenId_ : QStringLiteral("None");
}

QString SystrayWidget::stateFilePath() const {
  const QString dir = deps_.stateDir.isEmpty() ? appDataDir() : deps_.stateDir;
  return SystrayStateStore(dir, screenId_ ? *screenId_ : QStringLiteral("None")).path();
}

void SystrayWidget::saveState() {
  updateCurrentState();
  const QString dir = deps_.stateDir.isEmpty() ? appDataDir() : deps_.stateDir;
  const SystrayStateStore store(dir, screenId_ ? *screenId_ : QStringLiteral("None"));
  qDebug().noquote() << "Saving systray state to" << store.path();
  if (!store.save(currentState_))
    qWarning().noquote() << "Systray state could not be written to" << store.path();
}

void SystrayWidget::loadState() {
  const QString id = screenId();
  const QString dir = deps_.stateDir.isEmpty() ? appDataDir() : deps_.stateDir;
  const SystrayStateStore store(dir, id);
  qDebug().noquote() << "Loading systray state from" << store.path();
  currentState_ = store.load();
  updateCurrentState();
}

void SystrayWidget::addIconToUnpinned(TrayIconButton* icon) {
  if (options_.show_in_popup)
    popup_->addIcon(icon);
  else
    unpinnedWidget_->rowLayout()->addWidget(icon);
}

void SystrayWidget::relayoutPopupGrid() {
  if (popup_) popup_->relayoutGrid();
}

}  // namespace yasb::widgets
