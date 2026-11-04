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
#include <vector>

namespace waku::bluetooth {

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
  // Shared by the classic and LE nodes of one physical device (empty when unknown).
  QString containerId;
  // Addresses of the entries merged into this row (mergeSameDevices); the row keeps the classic
  // one.
  QStringList otherAddresses;
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
  // The radio state could not be read and no device was listed (the stack may not be ready).
  // The service retries; radioOn and devices keep their last values meanwhile.
  bool radioUnknown = false;
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

// The message of a pairing that Windows cannot finish without its own UI (a PIN, a passkey
// comparison, a confirmation): the user pairs the device in Windows Settings.
QString pairNeedsSettingsMessage();

// Pure helpers of bluetooth_types.py.
QString formatBtAddress(quint64 address);

// True when `name` is no name: empty, the address itself, or "Bluetooth <address>", in any case.
// Windows hands that out for a device it is not connected to, while the real name sits on the
// device's PnP node.
inline bool isPlaceholderBtName(const QString& name, const QString& address) {
  const QString trimmed = name.trimmed();
  if (trimmed.isEmpty() || trimmed.compare(address, Qt::CaseInsensitive) == 0) return true;
  const QString prefix = QStringLiteral("Bluetooth ");
  return trimmed.startsWith(prefix, Qt::CaseInsensitive) &&
         trimmed.mid(prefix.size()).compare(address, Qt::CaseInsensitive) == 0;
}
std::pair<int, int> parseClassOfDevice(quint32 classOfDevice);
BtDeviceType deviceTypeFromCod(int major, int minor);
BtDeviceType deviceTypeFromLeAppearance(int category, int subcategory);
QStringList profilesFromCod(int major, bool connected);
bool preferDevice(const BluetoothDevice& candidate, const BluetoothDevice& existing);
// Collapses the classic and LE entries of one physical device into one row. Entries with the same
// container id merge. Entries without one merge when a classic and an LE entry share the display
// name and the device type (a Generic type matches any). Distinct devices that only share a name
// stay apart when their container ids differ. Order of first appearance is kept.
std::vector<BluetoothDevice> mergeSameDevices(const std::vector<BluetoothDevice>& devices);

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
  // Pairs a nearby device by its address with Windows' default pairing (no PIN UI of our own);
  // pairingFinished follows. A device that needs a PIN or a confirmation fails with
  // pairNeedsSettingsMessage(). Returns false when another pair, forget or connection runs.
  virtual bool pairDevice(const QString& address) = 0;
  // Forgets a paired device; pairingFinished follows. Same busy rule as pairDevice.
  virtual bool unpairDevice(const QString& address) = 0;
  // Cancels a running pair. Safe from the GUI thread; pairingFinished reports "Pairing cancelled".
  virtual void cancelPair() = 0;
  virtual bool hasPendingScan() const { return false; }

  // Last published snapshot.
  bool isRadioOn() const { return status_.radioOn; }
  QVector<BluetoothDevice> devices() const { return status_.devices; }

 signals:
  void scanStarted();
  void scanCompleted(int status, const QVector<waku::bluetooth::BluetoothDevice>& devices);
  void statusUpdated(const waku::bluetooth::BluetoothStatus& status);
  void refreshFailed(const QString& message);
  void connectionFinished(bool success, const QString& message,
                          const waku::bluetooth::BluetoothDevice& device);
  // The end of pairDevice() or unpairDevice().
  void pairingFinished(bool success, const QString& message,
                       const waku::bluetooth::BluetoothDevice& device);

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

}  // namespace waku::bluetooth

Q_DECLARE_METATYPE(waku::bluetooth::BluetoothDevice)
Q_DECLARE_METATYPE(waku::bluetooth::BluetoothStatus)
Q_DECLARE_METATYPE(QVector<waku::bluetooth::BluetoothDevice>)
