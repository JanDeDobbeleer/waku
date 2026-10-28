#include "widgets/bluetooth/bluetooth_menu.h"

#include <QCursor>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QSizePolicy>
#include <QUrl>
#include <algorithm>

#include "core/bar/theme_class.h"

namespace yasb::widgets {
namespace {

using platform::BluetoothDevice;

QString qs(const std::string& text) { return QString::fromStdString(text); }

// bluetooth_widgets._battery_icon: (css level, glyph) for a percentage.
std::pair<QString, QString> batteryIcon(int percent,
                                        const config::options::BluetoothBatteryIconsConfig& icons) {
  if (percent <= 10) return {QStringLiteral("empty"), qs(icons.empty)};
  if (percent <= 30) return {QStringLiteral("low"), qs(icons.low)};
  if (percent <= 60) return {QStringLiteral("medium"), qs(icons.medium)};
  if (percent <= 90) return {QStringLiteral("high"), qs(icons.high)};
  return {QStringLiteral("full"), qs(icons.full)};
}

QString deviceIcon(const config::options::BluetoothDeviceIconsConfig& icons,
                   platform::BtDeviceType type) {
  using platform::BtDeviceType;
  switch (type) {
    case BtDeviceType::Headphones:
      return qs(icons.headphones);
    case BtDeviceType::Headset:
      return qs(icons.headset);
    case BtDeviceType::Speaker:
      return qs(icons.speaker);
    case BtDeviceType::Phone:
      return qs(icons.phone);
    case BtDeviceType::Tablet:
      return qs(icons.tablet);
    case BtDeviceType::Laptop:
      return qs(icons.laptop);
    case BtDeviceType::Computer:
      return qs(icons.computer);
    case BtDeviceType::Keyboard:
      return qs(icons.keyboard);
    case BtDeviceType::Mouse:
      return qs(icons.mouse);
    case BtDeviceType::Controller:
      return qs(icons.controller);
    case BtDeviceType::Watch:
      return qs(icons.watch);
    case BtDeviceType::Camera:
      return qs(icons.camera);
    case BtDeviceType::Generic:
      break;
  }
  return qs(icons.generic);
}

}  // namespace

// --- BluetoothItem ----------------------------------------------------------------------------

BluetoothItem::BluetoothItem(QWidget* parent) : QFrame(parent) {
  data_.name.clear();
  setProperty("class", QStringLiteral("bluetooth-item"));
  setCursor(QCursor(Qt::PointingHandCursor));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->setAlignment(Qt::AlignTop);

  auto* details = new QFrame(this);
  details->setProperty("class", QStringLiteral("details-container"));
  details->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* row = new QHBoxLayout(details);
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(0);

  icon_ = new QLabel(details);
  icon_->setProperty("class", QStringLiteral("icon"));
  icon_->setAlignment(Qt::AlignCenter);

  auto* textColumn = new QWidget(details);
  textColumn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  auto* text = new QVBoxLayout(textColumn);
  text->setContentsMargins(0, 0, 0, 0);
  text->setSpacing(0);
  name_ = new QLabel(textColumn);
  name_->setProperty("class", QStringLiteral("name"));
  status_ = new QLabel(textColumn);
  status_->setProperty("class", QStringLiteral("status"));
  text->addWidget(name_);
  text->addWidget(status_);

  batteryLabel_ = new QLabel(details);
  batteryLabel_->setProperty("class", QStringLiteral("battery"));
  batteryIcon_ = new QLabel(details);
  batteryIcon_->setProperty("class", QStringLiteral("battery-icon"));

  row->addWidget(icon_);
  row->addWidget(textColumn, 1);
  row->addWidget(batteryLabel_);
  row->addWidget(batteryIcon_);

  actions_ = new QFrame(this);
  actions_->setProperty("class", QStringLiteral("controls-container"));
  actions_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* actionsLayout = new QHBoxLayout(actions_);
  actionsLayout->setContentsMargins(0, 0, 0, 0);
  actionsLayout->setSpacing(0);
  connect_ = new QPushButton(QStringLiteral("Connect"), actions_);
  connect_->setProperty("class", QStringLiteral("connect"));
  connect(connect_, &QPushButton::clicked, this, &BluetoothItem::onActionClicked);
  actionsLayout->addStretch(1);
  actionsLayout->addWidget(connect_);
  actions_->hide();

  root->addWidget(details);
  root->addWidget(actions_);
}

void BluetoothItem::setData(const BluetoothDevice& data) {
  data_ = data;
  name_->setText(data.name);
  icon_->setText(data.icon);
  status_->setText(data.statusText);
  const bool showBattery = data.battery.has_value() && data.connected;
  batteryLabel_->setVisible(showBattery);
  batteryIcon_->setVisible(showBattery);
  if (showBattery) batteryLabel_->setText(QStringLiteral("%1%").arg(*data.battery));
  if (active_ && !busy_) connect_->setText(actionText());
}

void BluetoothItem::setLabels(const BtLabels& labels) {
  labels_ = labels;
  if (active_ && !busy_) connect_->setText(actionText());
}

void BluetoothItem::setBusy(bool busy, bool disconnecting) {
  busy_ = busy;
  connect_->setEnabled(!busy);
  if (busy) {
    const auto it = labels_.find(disconnecting ? QStringLiteral("disconnecting")
                                               : QStringLiteral("connecting"));
    if (it != labels_.end()) connect_->setText(it->second);
  } else if (active_) {
    connect_->setText(actionText());
  }
}

void BluetoothItem::setActive(bool active) {
  if (active == active_) return;
  active_ = active;
  setProperty(
      "class",
      QStringLiteral("bluetooth-item%1").arg(active ? QStringLiteral(" active") : QString()));
  bar::refreshWidgetStyle(this);
  actions_->setVisible(active);
  if (active && !busy_) {
    connect_->setEnabled(true);
    connect_->setText(actionText());
  }
}

QString BluetoothItem::actionText() const {
  auto label = [this](const char* key) {
    const auto it = labels_.find(QString::fromLatin1(key));
    return it == labels_.end() ? QString() : it->second;
  };
  if (!data_.paired && !data_.remembered) return label("pair");
  if (!data_.supportsConnect()) return label("manage");
  if (data_.connected) return label("disconnect");
  return label("connect");
}

void BluetoothItem::onActionClicked() {
  if (busy_) return;
  if ((!data_.paired && !data_.remembered) || !data_.supportsConnect()) {
    emit settingsPressed(data_);
  } else if (data_.connected) {
    emit disconnectPressed(data_);
  } else {
    emit connectPressed(data_);
  }
}

void BluetoothItem::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (actions_->isVisible() && actions_->geometry().contains(event->pos())) return;
    emit clicked();
  }
  QFrame::mousePressEvent(event);
}

// --- BluetoothSection -------------------------------------------------------------------------

BluetoothSection::BluetoothSection(const QString& title, QWidget* parent) : QFrame(parent) {
  setProperty("class", QStringLiteral("section"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  layout_->setAlignment(Qt::AlignTop);
  auto* titleLabel = new QLabel(title, this);
  titleLabel->setProperty("class", QStringLiteral("section-title"));
  layout_->addWidget(titleLabel);
}

void BluetoothSection::sync(const std::map<QString, BluetoothDevice>& devices,
                            const config::options::BluetoothBatteryIconsConfig& batteryIcons,
                            const BtLabels& labels) {
  for (const QString& address : items_.keys()) {
    if (devices.count(address)) continue;
    BluetoothItem* item = items_.take(address);
    layout_->removeWidget(item);
    item->hide();
    item->deleteLater();
  }

  std::vector<const BluetoothDevice*> ordered;
  for (const auto& entry : devices) ordered.push_back(&entry.second);
  std::stable_sort(ordered.begin(), ordered.end(),
                   [](const BluetoothDevice* a, const BluetoothDevice* b) {
                     if (a->connected != b->connected) return a->connected;
                     return a->name.toLower() < b->name.toLower();
                   });

  for (const BluetoothDevice* device : ordered) {
    BluetoothItem* item = items_.value(device->address, nullptr);
    if (!item) {
      item = new BluetoothItem(this);
      connect(item, &BluetoothItem::clicked, this, [this, item] { emit itemClicked(item); });
      connect(item, &BluetoothItem::connectPressed, this, &BluetoothSection::connectPressed);
      connect(item, &BluetoothItem::disconnectPressed, this, &BluetoothSection::disconnectPressed);
      connect(item, &BluetoothItem::settingsPressed, this, &BluetoothSection::settingsPressed);
      items_.insert(device->address, item);
      layout_->addWidget(item);
    }
    item->setLabels(labels);
    item->setData(*device);
    if (device->battery && device->connected) {
      const auto [level, glyph] = batteryIcon(*device->battery, batteryIcons);
      item->batteryIcon()->setText(glyph);
      item->batteryIcon()->setProperty("class", QStringLiteral("battery-icon %1").arg(level));
      bar::refreshWidgetStyle(item->batteryIcon());
    }
  }

  int index = 0;
  for (const BluetoothDevice* device : ordered) {
    layout_->insertWidget(++index, items_.value(device->address));
  }
}

void BluetoothSection::setActive(const QString& address) {
  for (auto it = items_.begin(); it != items_.end(); ++it)
    it.value()->setActive(it.key() == address);
}

// --- BluetoothList ----------------------------------------------------------------------------

BluetoothList::BluetoothList(const QString& pairedTitle, const QString& newTitle, QWidget* parent)
    : QScrollArea(parent) {
  setWidgetResizable(true);
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  setViewportMargins(0, 0, -4, 0);
  if (QWidget* port = viewport()) port->setAutoFillBackground(false);

  auto* container = new QFrame(this);
  container->setProperty("class", QStringLiteral("bluetooth-list"));
  container->setAutoFillBackground(false);
  auto* layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->setAlignment(Qt::AlignTop);

  paired_ = new BluetoothSection(pairedTitle, container);
  discovered_ = new BluetoothSection(newTitle, container);
  for (BluetoothSection* section : {paired_, discovered_}) {
    connect(section, &BluetoothSection::itemClicked, this, &BluetoothList::toggleItem);
    connect(section, &BluetoothSection::connectPressed, this, &BluetoothList::connectPressed);
    connect(section, &BluetoothSection::disconnectPressed, this, &BluetoothList::disconnectPressed);
    connect(section, &BluetoothSection::settingsPressed, this, &BluetoothList::settingsPressed);
    layout->addWidget(section);
  }
  layout->addStretch(1);
  setWidget(container);
}

void BluetoothList::toggleItem(BluetoothItem* item) {
  if (item->busy()) return;
  const QString address = item->active() ? QString() : item->data().address;
  paired_->setActive(paired_->items().contains(address) ? address : QString());
  discovered_->setActive(discovered_->items().contains(address) ? address : QString());
}

void BluetoothList::clearActive() {
  paired_->setActive(QString());
  discovered_->setActive(QString());
}

BluetoothItem* BluetoothList::findItem(const QString& address) const {
  if (BluetoothItem* item = paired_->items().value(address, nullptr)) return item;
  return discovered_->items().value(address, nullptr);
}

void BluetoothList::clearBusy() {
  for (BluetoothSection* section : {paired_, discovered_}) {
    for (BluetoothItem* item : section->items()) {
      if (item->busy()) item->setBusy(false);
    }
  }
}

// --- BluetoothMenu ----------------------------------------------------------------------------

BluetoothMenu::BluetoothMenu(QWidget* anchor, config::options::BluetoothMenuConfig config,
                             std::shared_ptr<platform::BluetoothBackend> backend)
    : QObject(anchor),
      anchor_(anchor),
      config_(std::move(config)),
      backend_(std::move(backend)),
      opener_([](const QString& uri) { QDesktopServices::openUrl(QUrl(uri)); }) {}

BtLabels BluetoothMenu::actionLabels() const {
  const auto& labels = config_.labels;
  return {{QStringLiteral("connect"), qs(labels.connect)},
          {QStringLiteral("disconnect"), qs(labels.disconnect)},
          {QStringLiteral("connecting"), qs(labels.connecting)},
          {QStringLiteral("disconnecting"), qs(labels.disconnecting)},
          {QStringLiteral("pair"), qs(labels.pair)},
          {QStringLiteral("manage"), qs(labels.manage)}};
}

void BluetoothMenu::showMenu() {
  if (detached_) return;
  if (popup_ && popup_->isVisible()) {
    popup_->hideAnimated();
    return;
  }
  if (!popup_) build();
  open();
}

void BluetoothMenu::build() {
  const auto& labels = config_.labels;
  popup_ = new MenuPopup(anchor_, config_.blur, config_.round_corners,
                         qs(config_.round_corners_type), qs(config_.border_color), false, true);
  popup_->setCssClass(QStringLiteral("bluetooth-menu"));

  auto* root = new QVBoxLayout(popup_);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* header = new QFrame(popup_);
  header->setProperty("class", QStringLiteral("header"));
  header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* headerRow = new QHBoxLayout(header);
  headerRow->setContentsMargins(0, 0, 0, 0);
  headerRow->setSpacing(0);

  auto* title = new QLabel(qs(labels.title), header);
  title->setProperty("class", QStringLiteral("title"));
  const bool radioOn = backend_->isRadioOn();
  power_ = new QPushButton(powerLabel(radioOn), header);
  power_->setCheckable(true);
  power_->setChecked(radioOn);
  power_->setCursor(QCursor(Qt::PointingHandCursor));
  power_->setProperty("class", QStringLiteral("power-button"));
  connect(power_, &QPushButton::toggled, this, &BluetoothMenu::toggleRadio);
  headerRow->addWidget(title, 1);
  headerRow->addWidget(power_);

  progress_ = new LoaderLine(header);
  progress_->configure(QStringLiteral("progress-bar"), 0.40, 2);
  progress_->attachToWidget(header);
  progress_->stop();

  error_ = new QLabel(popup_);
  error_->setAlignment(Qt::AlignCenter);
  error_->setProperty("class", QStringLiteral("error-message"));
  error_->hide();

  list_ = new BluetoothList(qs(labels.your_devices), qs(labels.new_devices), popup_);
  connect(list_, &BluetoothList::connectPressed, this, &BluetoothMenu::connectDevice);
  connect(list_, &BluetoothList::disconnectPressed, this, &BluetoothMenu::disconnectDevice);
  connect(list_, &BluetoothList::settingsPressed, this,
          [this](const BluetoothDevice&) { openUri(QStringLiteral("ms-settings:bluetooth")); });

  auto* footer = new QFrame(popup_);
  footer->setProperty("class", QStringLiteral("footer"));
  footer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  auto* footerRow = new QHBoxLayout(footer);
  footerRow->setContentsMargins(0, 0, 0, 0);
  footerRow->setSpacing(0);
  auto* settings = new QPushButton(qs(labels.more_settings), footer);
  settings->setProperty("class", QStringLiteral("settings-button"));
  connect(settings, &QPushButton::clicked, this,
          [this] { openUri(QStringLiteral("ms-settings:bluetooth")); });
  scan_ = new QPushButton(qs(config_.device_icons.scan), footer);
  scan_->setProperty("class", QStringLiteral("scan-icon"));
  connect(scan_, &QPushButton::clicked, this, &BluetoothMenu::scan);
  footerRow->addWidget(settings);
  footerRow->addStretch(1);
  footerRow->addWidget(scan_);

  root->addWidget(header);
  root->addWidget(error_);
  root->addWidget(list_, 1);
  root->addWidget(footer);
  popup_->installEventFilter(this);
  popup_->adjustSize();
}

bool BluetoothMenu::eventFilter(QObject* watched, QEvent* event) {
  if (watched == popup_ && event->type() == QEvent::Hide) {
    scanning_ = false;
    if (progress_) progress_->stop();
    if (list_) {
      list_->clearBusy();
      list_->clearActive();
    }
  }
  return QObject::eventFilter(watched, event);
}

void BluetoothMenu::open() {
  if (!popup_) return;
  if (list_) {
    list_->clearBusy();
    list_->clearActive();
  }
  setRadioUi(backend_->isRadioOn());
  const auto known = backend_->devices();
  if (!known.isEmpty()) {
    paired_.clear();
    for (const BluetoothDevice& device : known) {
      if (!device.address.isEmpty() && (device.paired || device.remembered))
        paired_[device.address] = decorate(device);
    }
    applyList();
  }
  popup_->adjustSize();
  popup_->setPosition(qs(config_.alignment), qs(config_.direction), config_.offset_left,
                      config_.offset_top);
  popup_->show();
  refresh();
}

void BluetoothMenu::refresh() {
  if (!popup_) return;
  inquiry_ = false;
  if (error_) error_->hide();
  startLoader();
  backend_->refresh();
}

void BluetoothMenu::scan() {
  if (!popup_) return;
  inquiry_ = true;
  if (error_) error_->hide();
  startLoader();
  backend_->scan();
}

void BluetoothMenu::startLoader() {
  if (progress_ && !scanning_) {
    progress_->start();
    scanning_ = true;
  }
}

void BluetoothMenu::stopLoader() {
  if (!scanning_) return;
  scanning_ = false;
  if (progress_) progress_->stop();
}

void BluetoothMenu::onScanStarted() {
  if (popup_ && popup_->isVisible()) startLoader();
}

void BluetoothMenu::onScanCompleted(int status, const QVector<BluetoothDevice>& devices) {
  if (!popup_) return;
  if (!backend_->hasPendingScan()) stopLoader();
  const auto result = static_cast<platform::BtScanStatus>(status);
  if (result == platform::BtScanStatus::RadioOff) {
    setRadioUi(false);
    paired_.clear();
    discovered_.clear();
    applyList();
    return;
  }
  if (result == platform::BtScanStatus::ApiUnavailable) {
    showError(QStringLiteral("Bluetooth API unavailable"));
    return;
  }
  if (result == platform::BtScanStatus::Error) {
    showError(QStringLiteral("Failed to scan for devices"));
    return;
  }
  setRadioUi(true);
  std::map<QString, BluetoothDevice> paired;
  std::map<QString, BluetoothDevice> discovered;
  for (const BluetoothDevice& device : devices) {
    if (device.address.isEmpty()) continue;
    const BluetoothDevice decorated = decorate(device);
    if (decorated.paired || decorated.remembered)
      paired[decorated.address] = decorated;
    else
      discovered[decorated.address] = decorated;
  }
  paired_ = paired;
  std::map<QString, BluetoothDevice> source = inquiry_ ? discovered : discovered_;
  discovered_.clear();
  for (const auto& entry : source) {
    if (!paired.count(entry.first)) discovered_[entry.first] = entry.second;
  }
  inquiry_ = false;
  applyList();
}

void BluetoothMenu::applyStatus(const platform::BluetoothStatus& status) {
  if (!popup_ || !popup_->isVisible()) return;
  setRadioUi(status.radioOn);
  if (!status.radioOn) {
    stopLoader();
    paired_.clear();
    discovered_.clear();
    applyList();
    return;
  }
  // The popup and the bar share one truth: always keep the paired list in sync.
  paired_.clear();
  for (const BluetoothDevice& device : status.devices) {
    if (!device.address.isEmpty() && (device.paired || device.remembered))
      paired_[device.address] = decorate(device);
  }
  if (!inquiry_) {
    for (auto it = discovered_.begin(); it != discovered_.end();) {
      it = paired_.count(it->first) ? discovered_.erase(it) : std::next(it);
    }
    stopLoader();
  }
  applyList();
}

void BluetoothMenu::onRefreshFailed(const QString& message) {
  if (!popup_ || !popup_->isVisible()) return;
  stopLoader();
  showError(message.isEmpty() ? QStringLiteral("Bluetooth refresh failed") : message);
}

void BluetoothMenu::connectDevice(const BluetoothDevice& device) {
  if (BluetoothItem* item = list_ ? list_->findItem(device.address) : nullptr)
    item->setBusy(true, false);
  backend_->connectDevice(device.address);
}

void BluetoothMenu::disconnectDevice(const BluetoothDevice& device) {
  if (BluetoothItem* item = list_ ? list_->findItem(device.address) : nullptr)
    item->setBusy(true, true);
  backend_->disconnectDevice(device.address);
}

void BluetoothMenu::onConnectionFinished(bool success, const QString& message,
                                         const BluetoothDevice& device) {
  if (!popup_ || !popup_->isVisible()) return;
  if (BluetoothItem* item = list_ ? list_->findItem(device.address) : nullptr) item->setBusy(false);
  if (!success) {
    showError(message);
    return;
  }
  if (!device.address.isEmpty() && (device.paired || device.remembered))
    paired_[device.address] = decorate(device);
  applyList();
}

void BluetoothMenu::toggleRadio(bool checked) {
  if (power_) {
    power_->setText(powerLabel(checked));
    bar::refreshWidgetStyle(power_);
  }
  startLoader();
  if (!backend_->setRadio(checked)) {
    setRadioUi(!checked);
    stopLoader();
    showError(QStringLiteral("Unable to change Bluetooth power"));
  }
}

BluetoothDevice BluetoothMenu::decorate(const BluetoothDevice& source) const {
  BluetoothDevice device = source;
  device.icon = deviceIcon(config_.device_icons, device.deviceType);
  const QString connected = qs(config_.labels.connected);
  if (device.connected && !device.profiles.isEmpty()) {
    device.statusText = connected + QLatin1Char(' ') + device.profiles.join(QStringLiteral(", "));
  } else if (device.connected) {
    device.statusText = connected;
  } else {
    device.statusText = qs(config_.labels.not_connected);
  }
  return device;
}

void BluetoothMenu::applyList() {
  if (!list_ || !popup_) return;
  const BtLabels labels = actionLabels();
  const auto& battery = config_.device_icons.battery;
  list_->paired()->sync(paired_, battery, labels);
  list_->discovered()->sync(discovered_, battery, labels);
}

QString BluetoothMenu::powerLabel(bool radioOn) const {
  return qs(radioOn ? config_.labels.power_on : config_.labels.power_off);
}

void BluetoothMenu::setRadioUi(bool radioOn) {
  if (power_) {
    if (power_->isChecked() != radioOn) {
      power_->blockSignals(true);
      power_->setChecked(radioOn);
      power_->blockSignals(false);
    }
    power_->setText(powerLabel(radioOn));
    bar::refreshWidgetStyle(power_);
  }
  if (list_) list_->setEnabled(radioOn);
}

void BluetoothMenu::showError(const QString& message) {
  if (!error_) return;
  error_->setText(message);
  error_->show();
  QTimer::singleShot(4000, this, [this] {
    if (error_) error_->hide();
  });
}

void BluetoothMenu::openUri(const QString& uri) {
  opener_(uri);
  if (popup_) popup_->hideAnimated();
}

}  // namespace yasb::widgets
