#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <memory>

#include "platform/bluetooth.h"

namespace wabar::services {

namespace platform = ::yasb::platform;

// The Bluetooth radio and its paired devices as a QML singleton, shared by every Bluetooth
// widget. Wraps platform::BluetoothBackend, which owns the enumeration thread (BluetoothAPIs and
// WinRT reads, battery, radio power, connect and disconnect); this object lives on the GUI thread
// and mirrors the backend's queued `statusUpdated` snapshots as one `changed`. The engine destroys
// it, which destroys the backend: cancel connect, shutdown, quit and join the thread.
//
// `available` is false (and the backend is never created) when kernel::liveDesktop("bluetooth")
// is false, and on a machine without a Bluetooth adapter. `devices` is a
// list of maps: id (the address, pass it back to connectDevice), name, connected, paired,
// canConnect (classic audio devices only), type (device type name) and batteryPercent (-1 when
// unknown), connected devices first. Unpaired devices are left out.
class Bluetooth : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
  Q_PROPERTY(bool available READ available CONSTANT FINAL)
  Q_PROPERTY(bool radioOn READ radioOn NOTIFY changed FINAL)
  Q_PROPERTY(QVariantList devices READ devices NOTIFY changed FINAL)
  Q_PROPERTY(int connectedCount READ connectedCount NOTIFY changed FINAL)
  Q_PROPERTY(QStringList connectedNames READ connectedNames NOTIFY changed FINAL)

 public:
  // Real backend, unless unavailable (see above).
  explicit Bluetooth(QObject* parent = nullptr);
  // Test seam: any backend (null = unavailable). Started at once.
  Bluetooth(std::unique_ptr<platform::BluetoothBackend> backend, QObject* parent);
  ~Bluetooth() override;

  bool available() const { return backend_ != nullptr; }
  bool radioOn() const { return status_.radioOn; }
  QVariantList devices() const;
  int connectedCount() const { return static_cast<int>(connectedDevices().size()); }
  QStringList connectedNames() const;

  Q_INVOKABLE void setRadio(bool on);
  Q_INVOKABLE void connectDevice(const QString& id);
  Q_INVOKABLE void disconnectDevice(const QString& id);

 signals:
  void changed();

 private:
  QVector<platform::BluetoothDevice> connectedDevices() const;
  void onStatus(const platform::BluetoothStatus& status);

  std::unique_ptr<platform::BluetoothBackend> backend_;
  platform::BluetoothStatus status_;
};

}  // namespace wabar::services
