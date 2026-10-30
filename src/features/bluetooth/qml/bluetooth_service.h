#pragma once

#include <QtQml/qqmlregistration.h>

#include <QList>
#include <QMetaMethod>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <memory>

#include "features/bluetooth/bluetooth.h"

namespace wabar::bluetooth {

// The Bluetooth radio and its paired devices as a QML singleton, shared by every Bluetooth
// widget. Wraps BluetoothBackend, which owns the enumeration thread (BluetoothAPIs and
// WinRT reads, battery, radio power, connect and disconnect); this object lives on the GUI thread
// and mirrors the backend's queued `statusUpdated` snapshots as one `changed`. The engine destroys
// it, which destroys the backend: cancel connect, shutdown, quit and join the thread.
//
// `available` is false (and the backend is never created) when kernel::liveDesktop("bluetooth")
// is false, and on a machine without a Bluetooth adapter. `devices` is a
// list of maps: id (the address, pass it back to connectDevice), name, connected, paired,
// canConnect (classic audio devices only), type (device type name) and batteryPercent (-1 when
// unknown), connected devices first. Unpaired devices are left out.
//
// Start-up: when Windows reports neither a radio state nor a device (the stack is not ready), the
// backend flags the snapshot `radioUnknown`. The service asks again after 1, 3, 8 and 20 s and
// shows "off" only when those retries find nothing. While `changed` has a listener, a 60 s
// timer reloads the list as a safety net; unobserved, nothing polls.
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
  Bluetooth(std::unique_ptr<BluetoothBackend> backend, QObject* parent);
  ~Bluetooth() override;

  bool available() const { return backend_ != nullptr; }
  bool radioOn() const { return status_.radioOn; }
  QVariantList devices() const;
  int connectedCount() const { return static_cast<int>(connectedDevices().size()); }
  QStringList connectedNames() const;

  Q_INVOKABLE void setRadio(bool on);
  Q_INVOKABLE void connectDevice(const QString& id);
  Q_INVOKABLE void disconnectDevice(const QString& id);

  // Test seams: the retry delays (ms) and the observed safety-net interval (ms).
  void setRetrySchedule(const QList<int>& delaysMs) { retryDelays_ = delaysMs; }
  void setSafetyInterval(int ms) { safetyTimer_.setInterval(ms); }
  bool safetyTimerActive() const { return safetyTimer_.isActive(); }

 signals:
  void changed();

 protected:
  void connectNotify(const QMetaMethod& signal) override;
  void disconnectNotify(const QMetaMethod& signal) override;

 private:
  bool observed() const;
  void updateSafetyTimer();
  void retryTick();
  QVector<BluetoothDevice> connectedDevices() const;
  void onStatus(const BluetoothStatus& status);

  std::unique_ptr<BluetoothBackend> backend_;
  BluetoothStatus status_;
  QList<int> retryDelays_{1000, 3000, 8000, 20000};
  int retriesDone_ = 0;
  QTimer retryTimer_;
  QTimer safetyTimer_;
};

}  // namespace wabar::bluetooth
