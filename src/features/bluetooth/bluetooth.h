#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>
#include <optional>
#include <utility>

namespace wabar::bluetooth {

// Port of bluetooth_types.DeviceType (the string names double as the device_icons keys).
enum class BtDeviceType {
  Headphones,
  Headset,
  Speaker,
  Phone,
  Tablet,
  Laptop,
  Computer,
  Keyboard,
  Mouse,
  Controller,
  Watch,
  Camera,
  Generic,
};

QString btDeviceTypeName(BtDeviceType type);

// Port of bluetooth_types.DeviceInfo.
struct BluetoothDevice {
  QString name;
  QString address;
  quint64 addressInt = 0;
  bool connected = false;
  bool paired = false;
  bool remembered = false;
  QString deviceId;
  int majorClass = 0;
  BtDeviceType deviceType = BtDeviceType::Generic;
  QString icon;
  QString statusText = QStringLiteral("Not connected");
  std::optional<int> battery;
  bool isLe = false;
  QStringList profiles;

  // Classic audio devices can be connected and disconnected from the menu.
  bool supportsConnect() const {
    if (isLe) return false;
    return deviceType == BtDeviceType::Headphones || deviceType == BtDeviceType::Headset ||
           deviceType == BtDeviceType::Speaker;
  }
  bool operator==(const BluetoothDevice&) const = default;
};

// Port of bluetooth_types.BluetoothStatus.
struct BluetoothStatus {
  bool radioOn = false;
  QVector<BluetoothDevice> devices;

  QVector<BluetoothDevice> connectedDevices() const {
    QVector<BluetoothDevice> result;
    for (const BluetoothDevice& device : devices) {
      if (device.connected && (device.paired || device.remembered)) result.push_back(device);
    }
    return result;
  }
};

enum class BtScanStatus { Success, RadioOff, ApiUnavailable, Error };

// Pure helpers of bluetooth_types.py.
QString formatBtAddress(quint64 address);
std::pair<int, int> parseClassOfDevice(quint32 classOfDevice);
BtDeviceType deviceTypeFromCod(int major, int minor);
BtDeviceType deviceTypeFromLeAppearance(int category, int subcategory);
QStringList profilesFromCod(int major, bool connected);
bool preferDevice(const BluetoothDevice& candidate, const BluetoothDevice& existing);

// Asynchronous Bluetooth operations behind the menu and the bar label (port of BluetoothManager).
// All methods return at once; work runs on a backend thread and results come back as queued
// signals. Tests substitute a fake; the real backend never runs in tests.
class BluetoothBackend : public QObject {
  Q_OBJECT
 public:
  using QObject::QObject;

  // Starts watching the adapter and loads the first snapshot.
  virtual void start() = 0;
  // Reloads the paired devices.
  virtual void refresh() = 0;
  // Inquiry scan for new devices; scanCompleted follows.
  virtual void scan() = 0;
  // False when another power change or connection is running.
  virtual bool setRadio(bool on) = 0;
  virtual void connectDevice(const QString& address) = 0;
  virtual void disconnectDevice(const QString& address) = 0;
  virtual bool hasPendingScan() const { return false; }

  // Last published snapshot.
  bool isRadioOn() const { return status_.radioOn; }
  QVector<BluetoothDevice> devices() const { return status_.devices; }

 signals:
  void scanStarted();
  void scanCompleted(int status, const QVector<wabar::bluetooth::BluetoothDevice>& devices);
  void statusUpdated(const wabar::bluetooth::BluetoothStatus& status);
  void refreshFailed(const QString& message);
  void connectionFinished(bool success, const QString& message,
                          const wabar::bluetooth::BluetoothDevice& device);

 protected:
  void publishStatus(const BluetoothStatus& status) {
    status_ = status;
    emit statusUpdated(status);
  }

 private:
  BluetoothStatus status_;
};

std::unique_ptr<BluetoothBackend> createBluetoothBackend();

// True when the machine has a Bluetooth adapter (BluetoothFindFirstRadio, no WinRT, returns at
// once). The adapter being switched off still counts.
bool hasBluetoothAdapter();

}  // namespace wabar::bluetooth

Q_DECLARE_METATYPE(wabar::bluetooth::BluetoothDevice)
Q_DECLARE_METATYPE(wabar::bluetooth::BluetoothStatus)
Q_DECLARE_METATYPE(QVector<wabar::bluetooth::BluetoothDevice>)
