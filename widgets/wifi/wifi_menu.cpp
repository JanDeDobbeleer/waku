#include "widgets/wifi/wifi_menu.h"

#include <QDesktopServices>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QTimer>
#include <QUrl>
#include <QWidgetAction>
#include <algorithm>

#include "core/bar/theme_class.h"

namespace yasb::widgets {
namespace {

using platform::kWifiConnected;
using platform::kWifiSecured;
using platform::WifiNetwork;

QString hiddenNetworkName() { return QStringLiteral("<Hidden Network>"); }

// Toggles a checkbox when the container around it is pressed (wifi_widgets.ClickToggleFilter).
class ClickToggleFilter : public QObject {
 public:
  ClickToggleFilter(QCheckBox* box, QObject* parent) : QObject(parent), box_(box) {}

 protected:
  bool eventFilter(QObject*, QEvent* event) override {
    if (event->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
      if (box_) box_->toggle();
      return true;
    }
    return false;
  }

 private:
  QPointer<QCheckBox> box_;
};

}  // namespace

// --- ClickableLabel ---------------------------------------------------------------------------

void ClickableLabel::setClickable(bool value) {
  setCursor(value ? Qt::PointingHandCursor : Qt::ArrowCursor);
  clickable_ = value;
}

void ClickableLabel::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && clickable_) {
    emit clicked();
    return;
  }
  QLabel::mousePressEvent(event);
}

// --- WifiItem ---------------------------------------------------------------------------------

WifiItem::WifiItem(QWidget* parent) : QFrame(parent) {
  setProperty("class", QStringLiteral("wifi-item"));
  setContentsMargins(0, 0, 0, 0);

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

  details_ = new QFrame(this);
  details_->setContentsMargins(0, 0, 0, 0);
  details_->setProperty("class", QStringLiteral("details-container"));
  auto* detailsLayout = new QHBoxLayout(details_);
  detailsLayout->setSpacing(0);
  detailsLayout->setContentsMargins(0, 0, 0, 0);

  icon_ = new QLabel(this);
  icon_->setProperty("class", QStringLiteral("icon"));
  icon_->setAlignment(Qt::AlignCenter);
  icon_->setContentsMargins(0, 0, 0, 0);

  name_ = new ElidedLabel(this);
  name_->setProperty("class", QStringLiteral("name"));

  auto* rightContainer = new QFrame(this);
  rightContainer->setProperty("class", QStringLiteral("right-container"));
  rightContainer->setContentsMargins(0, 0, 0, 0);
  auto* rightLayout = new QVBoxLayout(rightContainer);
  rightLayout->setSpacing(0);
  rightLayout->setContentsMargins(0, 0, 0, 0);

  statusLabel_ = new QLabel(this);
  statusLabel_->setProperty("class", QStringLiteral("status"));
  statusLabel_->setAlignment(Qt::AlignCenter | Qt::AlignRight);
  statusLabel_->setContentsMargins(0, 0, 0, 0);
  statusLabel_->setText(QStringLiteral("N/A"));

  strengthLabel_ = new QLabel(this);
  strengthLabel_->setProperty("class", QStringLiteral("strength"));
  strengthLabel_->setAlignment(Qt::AlignCenter | Qt::AlignRight);
  strengthLabel_->setContentsMargins(0, 0, 0, 0);
  strengthLabel_->setText(QStringLiteral("N/A"));

  rightLayout->addWidget(statusLabel_);
  rightLayout->addWidget(strengthLabel_);
  detailsLayout->addWidget(icon_);
  detailsLayout->addWidget(name_, 1);
  detailsLayout->addWidget(rightContainer);

  controls_ = new QFrame(this);
  controls_->setContentsMargins(0, 0, 0, 0);
  controls_->setProperty("class", QStringLiteral("controls-container"));
  auto* controlsLayout = new QHBoxLayout(controls_);
  controlsLayout->setSpacing(0);
  controlsLayout->setContentsMargins(0, 0, 0, 0);

  ssid_ = new QLineEdit(this);
  ssid_->setPlaceholderText(QStringLiteral("SSID"));
  ssid_->setProperty("class", QStringLiteral("password"));
  connect(ssid_, &QLineEdit::returnPressed, this, &WifiItem::manageConnectionClick);

  password_ = new QLineEdit(this);
  password_->setPlaceholderText(QStringLiteral("Password"));
  password_->setProperty("class", QStringLiteral("password"));
  connect(password_, &QLineEdit::returnPressed, this, &WifiItem::manageConnectionClick);
  password_->setEchoMode(QLineEdit::Password);

  connect_ = new QPushButton(this);
  connect_->setProperty("class", QStringLiteral("connect"));
  connect_->setText(QStringLiteral("Connect"));
  connect(connect_, &QPushButton::clicked, this, &WifiItem::manageConnectionClick);

  controlsLayout->addWidget(ssid_);
  controlsLayout->addWidget(password_);
  controlsLayout->addStretch();
  controlsLayout->addWidget(connect_);

  mainLayout->addWidget(details_);
  mainLayout->addWidget(controls_);
}

void WifiItem::setData(const WifiNetwork& data) {
  data_ = data;
  state_ = data.state;
  name_->setFullText(data.ssid);
  strengthLabel_->setText(QStringLiteral("%1%").arg(data.quality));
  icon_->setText(data.icon);
  updateState();
}

void WifiItem::manageConnectionClick() {
  if (state_ & kWifiConnected) {
    emit disconnectPressed(data_);
  } else {
    emit connectPressed(data_, password_->text(), ssid_->text());
  }
}

void WifiItem::updateState(std::optional<bool> setActive) {
  if (setActive) active_ = *setActive;
  setProperty("active", active_);
  controls_->setVisible(active_);
  if (active_) {
    const bool requiresPass = !(state_ & kWifiConnected) && (state_ & kWifiSecured);
    password_->setVisible(requiresPass && !data_.profileExists);
    connect_->setText((state_ & kWifiConnected) ? QStringLiteral("Disconnect")
                                                : QStringLiteral("Connect"));
  }
  if (state_ == kWifiConnected) {
    statusLabel_->setText(QStringLiteral("Connected"));
  } else if (state_ == (kWifiConnected | kWifiSecured)) {
    statusLabel_->setText(QStringLiteral("Connected, Secured"));
  } else if (state_ == kWifiSecured) {
    statusLabel_->setText(QStringLiteral("Secured"));
  } else {
    statusLabel_->setText(QStringLiteral("Unsecured"));
  }
  ssid_->setVisible(data_.ssid == hiddenNetworkName());
  bar::refreshWidgetStyle(this);
}

void WifiItem::mousePressEvent(QMouseEvent* event) {
  QFrame::mousePressEvent(event);
  if (event->button() == Qt::LeftButton) {
    emit clicked();
  } else if (event->button() == Qt::RightButton) {
    showContextMenu(event->pos());
  }
}

void WifiItem::showContextMenu(const QPoint& pos) {
  QMenu menu(this);
  menu.setProperty("class", QStringLiteral("context-menu"));

  autoConnectBox_ = new QCheckBox(QStringLiteral("Auto-connect"));
  autoConnectBox_->setChecked(data_.autoConnect);
  connect(autoConnectBox_, &QCheckBox::clicked, this, &WifiItem::toggleAutoConnect);
  autoConnectBox_->setProperty("class", QStringLiteral("checkbox"));

  auto* container = new QWidget(&menu);
  container->setProperty("class", QStringLiteral("menu-checkbox"));
  auto* layout = new QHBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setAlignment(Qt::AlignCenter);
  layout->addWidget(autoConnectBox_);
  container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  container->installEventFilter(new ClickToggleFilter(autoConnectBox_, container));

  auto* autoAction = new QWidgetAction(&menu);
  autoAction->setDefaultWidget(container);
  menu.addAction(autoAction);

  QAction* forget = menu.addAction(QStringLiteral("Forget Network"));
  forget->setEnabled(data_.profileExists);
  connect(forget, &QAction::triggered, this, [this, &menu] {
    emit forgetPressed(data_);
    menu.close();
  });

  menu.exec(mapToGlobal(pos));
}

void WifiItem::toggleAutoConnect() {
  data_.autoConnect = !data_.autoConnect;
  emit autoConnectToggled(data_);
  if (autoConnectBox_) {
    autoConnectBox_->setDisabled(true);
    QTimer::singleShot(3000, this, [box = autoConnectBox_] {
      if (box) box->setEnabled(true);
    });
  }
}

// --- WifiList ---------------------------------------------------------------------------------

WifiList::WifiList(QWidget* parent) : QScrollArea(parent) {
  setWidgetResizable(true);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setViewportMargins(0, 0, -4, 0);
  setStyleSheet(QStringLiteral(
      "QScrollArea { background: transparent; border: none; border-radius:0; }"
      "QScrollBar:vertical { border: none; background:transparent; width: 4px; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
      "QScrollBar::handle:vertical { background: rgba(255, 255, 255, 0.2); min-height: 10px; "
      "border-radius: 2px; }"
      "QScrollBar::handle:vertical:hover { background: rgba(255, 255, 255, 0.35); }"
      "QScrollBar::sub-line:vertical, QScrollBar::add-line:vertical { height: 0px; }"
      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));

  container_ = new QFrame(this);
  container_->setProperty("class", QStringLiteral("wifi-list"));
  // Never squeeze items below their size hint once the list scrolls. A CSS min-height becomes
  // each item's minimum, which would clip the expanded item's controls.
  container_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
  layout_ = new QVBoxLayout(container_);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(2);
  layout_->addStretch();
  setWidget(container_);
}

void WifiList::addItem(WifiItem* item) {
  connect(item, &WifiItem::clicked, this, [this, item] { onItemClicked(item); });
  connect(item, &WifiItem::disconnectPressed, this, &WifiList::disconnectPressed);
  connect(item, &WifiItem::connectPressed, this, &WifiList::connectPressed);
  connect(item, &WifiItem::forgetPressed, this, &WifiList::forgetPressed);
  connect(item, &WifiItem::autoConnectToggled, this, &WifiList::autoConnectToggled);
  item->setParent(container_);
  layout_->insertWidget(layout_->count() - 1, item);
  item->show();
  items_.push_back(item);
}

void WifiList::updateOrAddItem(const WifiNetwork& network, bool activeIfNew) {
  if (WifiItem* existing = item(network.ssid)) {
    existing->setData(network);
    return;
  }
  auto* created = new WifiItem(container_);
  created->setData(network);
  created->setActive(activeIfNew);
  addItem(created);
}

void WifiList::removeItem(const QString& ssid) {
  const auto it = std::find_if(items_.begin(), items_.end(),
                               [&](const WifiItem* i) { return i->data().ssid == ssid; });
  if (it == items_.end()) return;
  WifiItem* removed = *it;
  items_.erase(it);
  layout_->removeWidget(removed);
  removed->hide();
  removed->deleteLater();
}

WifiItem* WifiList::item(const QString& ssid) const {
  const auto it = std::find_if(items_.begin(), items_.end(),
                               [&](const WifiItem* i) { return i->data().ssid == ssid; });
  return it == items_.end() ? nullptr : *it;
}

void WifiList::sortItems() {
  std::stable_sort(items_.begin(), items_.end(), [](const WifiItem* a, const WifiItem* b) {
    return a->data().quality > b->data().quality;
  });
  // The current connection goes to the top, a hidden network to the bottom.
  auto connected = std::find_if(items_.begin(), items_.end(), [](const WifiItem* i) {
    return (i->data().state & kWifiConnected) != 0;
  });
  if (connected != items_.end()) std::rotate(items_.begin(), connected, connected + 1);
  auto hidden = std::find_if(items_.begin(), items_.end(), [](const WifiItem* i) {
    return i->data().ssid == hiddenNetworkName();
  });
  if (hidden != items_.end()) std::rotate(hidden, hidden + 1, items_.end());
  for (WifiItem* i : items_) layout_->removeWidget(i);
  for (WifiItem* i : items_) {
    layout_->insertWidget(layout_->count() - 1, i);
    i->show();
  }
}

void WifiList::clearItems() {
  for (WifiItem* i : items_) {
    layout_->removeWidget(i);
    i->hide();
    i->deleteLater();
  }
  items_.clear();
}

void WifiList::clearFields() {
  for (WifiItem* i : items_) {
    i->passwordField()->clear();
    i->ssidField()->clear();
  }
}

void WifiList::activateItem(const QString& name) {
  for (WifiItem* i : items_) i->setActive(i->data().ssid == name);
}

void WifiList::onItemClicked(WifiItem* selected) {
  for (WifiItem* i : items_) i->updateState(i == selected);
}

// --- WifiMenu ---------------------------------------------------------------------------------

WifiMenu::WifiMenu(QWidget* anchor, WifiMenuOptions options, BackendFactory backendFactory)
    : QObject(anchor),
      anchor_(anchor),
      options_(std::move(options)),
      backendFactory_(std::move(backendFactory)),
      opener_([](const QString& uri) { QDesktopServices::openUrl(QUrl(uri)); }),
      listUpdateTimer_(new QTimer(this)) {
  listUpdateTimer_->setSingleShot(true);
  connect(listUpdateTimer_, &QTimer::timeout, this, &WifiMenu::requestListUpdate);
}

WifiMenu::~WifiMenu() = default;

platform::WifiBackend* WifiMenu::backend() {
  if (!backend_) {
    backend_ = backendFactory_ ? backendFactory_() : platform::createWlanBackend();
    connect(backend_.get(), &platform::WifiBackend::scanCompleted, this,
            &WifiMenu::onScanCompleted);
    connect(backend_.get(), &platform::WifiBackend::currentConnection, this,
            [this](bool connected, const WifiNetwork& network) {
              updateItemsList(connected, network);
            });
    connect(backend_.get(), &platform::WifiBackend::connectFinished, this,
            [this](int status, const QString& ssid, const WifiNetwork& network) {
              connecting_ = false;
              onConnectionCompleted(status, ssid, network);
            });
    connect(backend_.get(), &platform::WifiBackend::connectFailed, this,
            [this](const QString&) { connecting_ = false; });
    connect(backend_.get(), &platform::WifiBackend::disconnected, this, &WifiMenu::onDisconnected);
  }
  return backend_.get();
}

WifiNetwork* WifiMenu::cached(const QString& ssid) {
  const auto it = std::find_if(cache_.begin(), cache_.end(),
                               [&](const WifiNetwork& n) { return n.ssid == ssid; });
  return it == cache_.end() ? nullptr : &*it;
}

void WifiMenu::showMenu() {
  if (MenuPopup* open = MenuPopup::openFor(anchor_)) {
    open->hideAnimated();
    return;
  }
  popup_ = new MenuPopup(anchor_, options_.blur, options_.roundCorners, options_.roundCornersType,
                         options_.borderColor, false);
  popup_->setCssClass(QStringLiteral("wifi-menu"));
  auto* mainLayout = new QVBoxLayout(popup_);
  mainLayout->setSpacing(0);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  auto* header = new QLabel(QStringLiteral("WiFi Networks"));
  header->setProperty("class", QStringLiteral("header"));

  progress_ = new LoaderLine(popup_);
  progress_->configure(QStringLiteral("progress-bar"), 0.40);
  progress_->setHidden(true);
  progress_->start();

  error_ = new ClickableLabel(popup_);
  error_->setAlignment(Qt::AlignCenter);
  error_->setProperty("class", QStringLiteral("error-message"));
  error_->setHidden(true);

  list_ = new WifiList(popup_);
  connect(list_, &WifiList::disconnectPressed, this, &WifiMenu::onDisconnect);
  connect(list_, &WifiList::connectPressed, this, &WifiMenu::onConnect);
  connect(list_, &WifiList::forgetPressed, this, &WifiMenu::onForget);
  connect(list_, &WifiList::autoConnectToggled, this, &WifiMenu::onAutoConnectToggled);

  auto* footer = new QFrame(popup_);
  footer->setProperty("class", QStringLiteral("footer"));
  auto* footerLayout = new QHBoxLayout(footer);
  footerLayout->setSpacing(0);
  footerLayout->setContentsMargins(0, 0, 0, 0);

  auto* settings = new QPushButton(QStringLiteral("More Wi-Fi settings"));
  settings->setProperty("class", QStringLiteral("settings-button"));
  connect(settings, &QPushButton::clicked, this,
          [this] { runAndHide(QStringLiteral("ms-settings:network-wifi")); });

  auto* refresh = new QPushButton(popup_);
  refresh->setText(QStringLiteral(""));
  refresh->setProperty("class", QStringLiteral("refresh-icon"));
  connect(refresh, &QPushButton::clicked, this, &WifiMenu::scanWifi);

  footerLayout->addWidget(settings);
  footerLayout->addStretch();
  footerLayout->addWidget(refresh);

  mainLayout->addWidget(header);
  mainLayout->addWidget(progress_);
  mainLayout->addWidget(error_);
  mainLayout->addWidget(list_);
  mainLayout->addWidget(footer);

  popup_->adjustSize();
  popup_->setPosition(options_.alignment, options_.direction, options_.offsetLeft,
                      options_.offsetTop);

  connect(popup_, &QObject::destroyed, this, [this] {
    if (backend_) backend_->setUpdatesEnabled(false);
  });
  backend()->setUpdatesEnabled(true);
  requestListUpdate();
  scanWifi();
  popup_->show();
}

void WifiMenu::showErrorBriefly(const QString& message) {
  if (!popup_ || !error_) return;
  error_->setClickable(false);
  error_->setText(message);
  error_->setVisible(true);
  progress_->setVisible(false);
  QTimer::singleShot(4000, this, [this] {
    if (popup_ && error_) error_->setVisible(false);
  });
}

void WifiMenu::onConnect(const WifiNetwork& network, const QString& password, const QString& ssid) {
  if (connecting_) return;
  connecting_ = true;
  const bool hidden = network.ssid == hiddenNetworkName();
  backend()->connectTo(network, hidden ? ssid : network.ssid, password, hidden);
  if (popup_ && progress_) progress_->setVisible(true);
}

void WifiMenu::onConnectionCompleted(int status, const QString& profileName,
                                     const WifiNetwork& network) {
  const auto result = static_cast<platform::WifiConnectStatus>(status);
  if (result != platform::WifiConnectStatus::Success) {
    if (result == platform::WifiConnectStatus::InvalidCredential) {
      showErrorBriefly(QStringLiteral("Invalid password"));
    } else if (result == platform::WifiConnectStatus::NetworkNotAvailable) {
      showErrorBriefly(QStringLiteral("Network not available"));
    }
    return;
  }
  if (WifiNetwork* entry = cached(profileName)) {
    entry->state |= kWifiConnected;
  } else {
    cache_.push_back(network);
  }
  if (!popup_ || !list_) return;
  if (WifiItem* item = list_->item(profileName)) {
    WifiNetwork data = item->data();
    data.state |= kWifiConnected;
    item->setData(data);
  }
  requestListUpdate();
  list_->activateItem(profileName);
  list_->clearFields();
  progress_->setVisible(false);
}

void WifiMenu::onDisconnect(const WifiNetwork& network) {
  backend()->disconnectCurrent();
  if (WifiNetwork* entry = cached(network.ssid)) {
    *entry = network;
    entry->state = network.state & ~kWifiConnected;
  }
}

void WifiMenu::onDisconnected(const QString& profileName) {
  if (WifiNetwork* entry = cached(profileName)) entry->state &= ~kWifiConnected;
  if (popup_ && list_) {
    if (WifiItem* item = list_->item(profileName)) {
      WifiNetwork data = item->data();
      data.state &= ~kWifiConnected;
      item->setData(data);
    }
  }
}

void WifiMenu::onForget(const WifiNetwork& network) {
  backend()->forget(network.ssid);
  WifiNetwork updated = network;
  updated.state = network.state & ~kWifiConnected;
  updated.profileExists = false;
  if (WifiNetwork* entry = cached(network.ssid)) *entry = updated;
  if (!popup_ || !list_) return;
  if (WifiItem* item = list_->item(network.ssid)) item->setData(updated);
}

void WifiMenu::scanWifi() {
  backend()->scan();
  if (popup_ && progress_) progress_->setVisible(true);
}

void WifiMenu::onScanCompleted(int status, const QVector<WifiNetwork>& networks) {
  const auto result = static_cast<platform::WifiScanStatus>(status);
  if (popup_ && result != platform::WifiScanStatus::Success) {
    auto setLink = [this](const QString& uri) {
      error_->setClickable(true);
      if (errorConnection_) disconnect(errorConnection_);
      errorConnection_ =
          connect(error_, &ClickableLabel::clicked, this, [this, uri] { runAndHide(uri); });
    };
    switch (result) {
      case platform::WifiScanStatus::AccessDenied:
        error_->setText(QStringLiteral("Error: Location services are disabled..."));
        setLink(QStringLiteral("ms-settings:privacy-location"));
        break;
      case platform::WifiScanStatus::PowerStateInvalid:
        error_->setText(QStringLiteral("Error: WiFi adapter is disabled..."));
        setLink(QStringLiteral("ms-settings:network-wifi"));
        break;
      case platform::WifiScanStatus::ServiceUnavailable:
        error_->setClickable(false);
        error_->setText(QStringLiteral("Wi-Fi is not available"));
        break;
      default:
        error_->setClickable(false);
        error_->setText(QStringLiteral("Unknown error..."));
        break;
    }
    list_->clearItems();
    cache_.clear();
    progress_->setVisible(false);
    error_->setVisible(true);
    return;
  }
  // Some adapters return an empty list; ignore it. Status updates do not touch the cache either.
  if (networks.isEmpty()) return;
  cache_.assign(networks.begin(), networks.end());
  listUpdateTimer_->start(100);  // Avoid unnecessary UI updates.
}

void WifiMenu::onAutoConnectToggled(const WifiNetwork& network) {
  if (WifiNetwork* entry = cached(network.ssid)) *entry = network;
  backend()->setAutoConnect(network.ssid, network.autoConnect);
}

void WifiMenu::requestListUpdate() {
  if (!popup_) return;
  backend()->queryCurrent();
}

void WifiMenu::updateItemsList(bool connected, const WifiNetwork& activeNetwork) {
  if (!popup_ || !list_) return;
  progress_->setVisible(false);
  if (connected) {
    WifiNetwork active = activeNetwork;
    const bool secured = (active.state & kWifiSecured) != 0;
    active.icon = wifiIcon(active.quality, secured);
    active.profileExists = true;  // Since it is the current connection.
    list_->updateOrAddItem(active, (active.state & kWifiConnected) != 0);
    if (WifiNetwork* entry = cached(active.ssid)) {
      *entry = active;
    } else {
      cache_.push_back(active);
    }
  }
  // Add cached networks except for the current one.
  for (WifiNetwork& network : cache_) {
    if (connected && network.ssid == activeNetwork.ssid) continue;
    network.icon = wifiIcon(network.quality, (network.state & kWifiSecured) != 0);
    list_->updateOrAddItem(network, (network.state & kWifiConnected) != 0);
  }
  // Clean up networks that are gone.
  std::vector<QString> stale;
  for (const WifiItem* item : list_->items()) {
    if (!cached(item->data().ssid)) stale.push_back(item->data().ssid);
  }
  for (const QString& ssid : stale) list_->removeItem(ssid);
  list_->sortItems();
  // Focus the first active item's password field.
  for (WifiItem* item : list_->items()) {
    if (!item->active()) continue;
    QLineEdit* password = item->passwordField();
    if (!password->isHidden() && !password->hasFocus()) password->setFocus();
    break;
  }
}

QString WifiMenu::wifiIcon(int strength, bool secured) const {
  const int level = std::min(strength / 25, 3);
  const QStringList& icons = secured ? options_.iconsSecured : options_.iconsUnsecured;
  return level >= 0 && level < icons.size() ? icons[level] : QString();
}

void WifiMenu::runAndHide(const QString& uri) {
  if (popup_) popup_->hideNow();
  opener_(uri);
}

}  // namespace yasb::widgets
