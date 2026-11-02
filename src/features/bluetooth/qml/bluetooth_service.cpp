#include "features/bluetooth/qml/bluetooth_service.h"

#include <QVariantMap>
#include <algorithm>

#include "kernel/qt/live_desktop.h"

namespace waku::bluetooth {

namespace {

std::unique_ptr<BluetoothBackend> makeRealBackend() {
  if (!kernel::liveDesktop(QStringLiteral("bluetooth"))) return nullptr;
  if (!hasBluetoothAdapter()) return nullptr;
  return createBluetoothBackend();
}

// Paired or remembered devices are the ones the bar lists and counts.
bool listed(const BluetoothDevice& device) { return device.paired || device.remembered; }

// True when `nearby` is the same physical device as one of the listed rows: the same address
// (also one merged into the row) or the same container id.
bool sameDevice(const BluetoothDevice& nearby, const BluetoothDevice& row) {
  if (nearby.address == row.address || row.otherAddresses.contains(nearby.address)) return true;
  return !nearby.containerId.isEmpty() && nearby.containerId == row.containerId;
}

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
  connect(backend_.get(), &BluetoothBackend::scanStarted, this, [this] { setScanning(true); });
  connect(backend_.get(), &BluetoothBackend::scanCompleted, this, &Bluetooth::onScanCompleted);
  connect(backend_.get(), &BluetoothBackend::pairingFinished, this, &Bluetooth::onPairingFinished);
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
  // Nearby devices belong to a radio that is on.
  if (!status.radioOn) nearby_.clear();
  emit changed();
}

void Bluetooth::onScanCompleted(int status, const QVector<BluetoothDevice>& found) {
  setScanning(false);
  switch (static_cast<BtScanStatus>(status)) {
    case BtScanStatus::Success: {
      nearby_.clear();
      for (const BluetoothDevice& device : found) {
        if (!listed(device) && !device.address.isEmpty()) nearby_.push_back(device);
      }
      emit changed();
      break;
    }
    case BtScanStatus::RadioOff:
      nearby_.clear();
      setError(QStringLiteral("Bluetooth is off"));
      emit changed();
      break;
    case BtScanStatus::ApiUnavailable:
      setError(QStringLiteral("Bluetooth scanning is not available"));
      break;
    case BtScanStatus::Error:
      setError(QStringLiteral("Bluetooth scan failed"));
      break;
  }
}

void Bluetooth::onPairingFinished(bool success, const QString& message,
                                  const BluetoothDevice& device) {
  setPairing(false);
  if (!success) {
    setError(message);
    return;
  }
  // The device is paired (or forgotten): it is not nearby any more. The refresh lists it.
  nearby_.removeIf([&](const BluetoothDevice& d) { return d.address == device.address; });
  if (backend_) backend_->refresh();
  emit changed();
}

void Bluetooth::setScanning(bool scanning) {
  if (scanning_ == scanning) return;
  scanning_ = scanning;
  emit scanningChanged();
}

void Bluetooth::setPairing(bool pairing) {
  if (pairing_ == pairing) return;
  pairing_ = pairing;
  emit pairingChanged();
}

void Bluetooth::setError(const QString& error) {
  if (error_ == error) return;
  error_ = error;
  emit errorChanged();
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
  QVector<BluetoothDevice> near;
  for (const BluetoothDevice& device : nearby_) {
    const bool repeated = std::any_of(shown.begin(), shown.end(), [&](const BluetoothDevice& row) {
      return sameDevice(device, row);
    });
    if (!repeated) near.push_back(device);
  }
  std::stable_sort(near.begin(), near.end(),
                   [](const BluetoothDevice& a, const BluetoothDevice& b) {
                     return a.name.localeAwareCompare(b.name) < 0;
                   });
  shown += near;
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

void Bluetooth::scan() {
  if (!backend_ || scanning_) return;
  setError(QString());
  setScanning(true);  // at once, so a second call in the same turn does nothing
  backend_->scan();
}

void Bluetooth::pairDevice(const QString& id) { startPairing(true, id); }

void Bluetooth::forgetDevice(const QString& id) { startPairing(false, id); }

void Bluetooth::startPairing(bool pair, const QString& id) {
  if (!backend_ || pairing_) return;
  setError(QString());
  const bool started = pair ? backend_->pairDevice(id) : backend_->unpairDevice(id);
  if (started)
    setPairing(true);
  else
    setError(QStringLiteral("Another Bluetooth operation is in progress"));
}

void Bluetooth::cancelPair() {
  if (backend_ && pairing_) backend_->cancelPair();
}

}  // namespace waku::bluetooth
