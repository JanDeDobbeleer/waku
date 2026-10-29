#include "spike/services/bluetooth_service.h"

#include <QGuiApplication>
#include <QVariantMap>
#include <algorithm>

namespace tidy::services {

namespace {

bool g_disabled = false;

std::unique_ptr<platform::BluetoothBackend> makeRealBackend() {
  if (g_disabled) return nullptr;
  if (QGuiApplication::platformName() == QStringLiteral("offscreen")) return nullptr;
  if (!platform::hasBluetoothAdapter()) return nullptr;
  return platform::createBluetoothBackend();
}

// Paired or remembered devices are the ones the bar lists and counts.
bool listed(const platform::BluetoothDevice& device) { return device.paired || device.remembered; }

}  // namespace

Bluetooth::Bluetooth(QObject* parent) : Bluetooth(makeRealBackend(), parent) {}

Bluetooth::Bluetooth(std::unique_ptr<platform::BluetoothBackend> backend, QObject* parent)
    : QObject(parent), backend_(std::move(backend)) {
  if (!backend_) return;
  connect(backend_.get(), &platform::BluetoothBackend::statusUpdated, this, &Bluetooth::onStatus);
  // The backend publishes the new connection through its watchers; ask once more so the list is
  // exact when a connect or disconnect ends.
  connect(backend_.get(), &platform::BluetoothBackend::connectionFinished, this,
          [this] { backend_->refresh(); });
  backend_->start();
}

Bluetooth::~Bluetooth() = default;

void Bluetooth::setDisabled(bool disabled) { g_disabled = disabled; }

void Bluetooth::onStatus(const platform::BluetoothStatus& status) {
  status_ = status;
  emit changed();
}

QVector<platform::BluetoothDevice> Bluetooth::connectedDevices() const {
  if (!status_.radioOn) return {};
  return status_.connectedDevices();
}

QStringList Bluetooth::connectedNames() const {
  QStringList names;
  for (const platform::BluetoothDevice& device : connectedDevices()) names.push_back(device.name);
  return names;
}

QVariantList Bluetooth::devices() const {
  QVector<platform::BluetoothDevice> shown;
  for (const platform::BluetoothDevice& device : status_.devices) {
    if (listed(device)) shown.push_back(device);
  }
  std::stable_sort(shown.begin(), shown.end(),
                   [](const platform::BluetoothDevice& a, const platform::BluetoothDevice& b) {
                     if (a.connected != b.connected) return a.connected;
                     return a.name.localeAwareCompare(b.name) < 0;
                   });
  QVariantList result;
  for (const platform::BluetoothDevice& device : shown) {
    QVariantMap map;
    map.insert(QStringLiteral("id"), device.address);
    map.insert(QStringLiteral("name"), device.name);
    map.insert(QStringLiteral("connected"), device.connected);
    map.insert(QStringLiteral("paired"), device.paired);
    map.insert(QStringLiteral("canConnect"), device.supportsConnect());
    map.insert(QStringLiteral("type"), platform::btDeviceTypeName(device.deviceType));
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

}  // namespace tidy::services
