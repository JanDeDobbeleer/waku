#include "features/bluetooth/qml/bluetooth_service.h"

#include <QVariantMap>
#include <algorithm>

#include "kernel/qt/live_desktop.h"

namespace wabar::bluetooth {

namespace {

std::unique_ptr<BluetoothBackend> makeRealBackend() {
  if (!kernel::liveDesktop(QStringLiteral("bluetooth"))) return nullptr;
  if (!hasBluetoothAdapter()) return nullptr;
  return createBluetoothBackend();
}

// Paired or remembered devices are the ones the bar lists and counts.
bool listed(const BluetoothDevice& device) { return device.paired || device.remembered; }

}  // namespace

Bluetooth::Bluetooth(QObject* parent) : Bluetooth(makeRealBackend(), parent) {}

Bluetooth::Bluetooth(std::unique_ptr<BluetoothBackend> backend, QObject* parent)
    : QObject(parent), backend_(std::move(backend)) {
  if (!backend_) return;
  retryTimer_.setSingleShot(true);
  connect(&retryTimer_, &QTimer::timeout, this, &Bluetooth::retryTick);
  safetyTimer_.setInterval(60000);
  connect(&safetyTimer_, &QTimer::timeout, this, [this] {
    if (!backend_ || !isSignalConnected(QMetaMethod::fromSignal(&Bluetooth::changed))) {
      updateSafetyTimer();
      return;
    }
    backend_->refresh();
  });
  connect(backend_.get(), &BluetoothBackend::statusUpdated, this, &Bluetooth::onStatus);
  // The backend publishes the new connection through its watchers; ask once more so the list is
  // exact when a connect or disconnect ends.
  connect(backend_.get(), &BluetoothBackend::connectionFinished, this,
          [this] { backend_->refresh(); });
  backend_->start();
}

Bluetooth::~Bluetooth() = default;

void Bluetooth::onStatus(const BluetoothStatus& status) {
  if (status.radioUnknown) {
    // Not known yet: keep what is shown and ask again, until the schedule runs out.
    if (retriesDone_ < retryDelays_.size()) {
      retryTimer_.start(retryDelays_.at(retriesDone_++));
      return;
    }
    BluetoothStatus off;
    status_ = off;
    emit changed();
    return;
  }
  retryTimer_.stop();
  retriesDone_ = 0;
  status_ = status;
  emit changed();
}

void Bluetooth::retryTick() {
  if (backend_) backend_->refresh();
}

bool Bluetooth::observed() const {
  return backend_ && isSignalConnected(QMetaMethod::fromSignal(&Bluetooth::changed));
}

void Bluetooth::updateSafetyTimer() {
  if (observed() == safetyTimer_.isActive()) return;
  if (observed())
    safetyTimer_.start();
  else
    safetyTimer_.stop();
}

void Bluetooth::connectNotify(const QMetaMethod& signal) {
  if (signal == QMetaMethod::fromSignal(&Bluetooth::changed)) updateSafetyTimer();
}

void Bluetooth::disconnectNotify(const QMetaMethod& signal) {
  // Qt calls this in the middle of a disconnect; the check runs from the event loop.
  if (signal == QMetaMethod::fromSignal(&Bluetooth::changed))
    QMetaObject::invokeMethod(this, &Bluetooth::updateSafetyTimer, Qt::QueuedConnection);
}

QVector<BluetoothDevice> Bluetooth::connectedDevices() const {
  if (!status_.radioOn) return {};
  return status_.connectedDevices();
}

QStringList Bluetooth::connectedNames() const {
  QStringList names;
  for (const BluetoothDevice& device : connectedDevices()) names.push_back(device.name);
  return names;
}

QVariantList Bluetooth::devices() const {
  QVector<BluetoothDevice> shown;
  for (const BluetoothDevice& device : status_.devices) {
    if (listed(device)) shown.push_back(device);
  }
  std::stable_sort(shown.begin(), shown.end(),
                   [](const BluetoothDevice& a, const BluetoothDevice& b) {
                     if (a.connected != b.connected) return a.connected;
                     return a.name.localeAwareCompare(b.name) < 0;
                   });
  QVariantList result;
  for (const BluetoothDevice& device : shown) {
    QVariantMap map;
    map.insert(QStringLiteral("id"), device.address);
    map.insert(QStringLiteral("name"), device.name);
    map.insert(QStringLiteral("connected"), device.connected);
    map.insert(QStringLiteral("paired"), device.paired);
    map.insert(QStringLiteral("canConnect"), device.supportsConnect());
    map.insert(QStringLiteral("type"), btDeviceTypeName(device.deviceType));
    map.insert(QStringLiteral("batteryPercent"), device.battery.value_or(-1));
    result.push_back(map);
  }
  return result;
}

void Bluetooth::setRadio(bool on) {
  if (backend_) backend_->setRadio(on);
}

void Bluetooth::connectDevice(const QString& id) {
  if (backend_) backend_->connectDevice(id);
}

void Bluetooth::disconnectDevice(const QString& id) {
  if (backend_) backend_->disconnectDevice(id);
}

}  // namespace wabar::bluetooth
