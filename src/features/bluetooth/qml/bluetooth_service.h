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
// unknown), connected devices first, then the nearby ones. A nearby device has `paired` false: it
// was found by the last scan() and is not paired yet (see Scanning and pairing).
//
// Scanning and pairing: scan() starts an inquiry on the backend thread and `scanning` is true until
// it ends; the unpaired devices it found join `devices` (a device that is listed already, by
// address or container id, is not repeated) until the next scan, a pair, or the radio going off.
// pairDevice(id) pairs a nearby device with Windows' default pairing and forgetDevice(id) forgets a
// paired one; `pairing` is true while either runs and cancelPair() stops it. A device that needs a
// PIN or a confirmation sets `error` to "This device needs pairing in Windows Settings". `error`
// holds the last scan, pair or forget failure and clears when the next one starts. Nothing scans
// on its own: only scan() does.
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
  Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged FINAL)
  Q_PROPERTY(bool pairing READ pairing NOTIFY pairingChanged FINAL)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged FINAL)

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
  bool scanning() const { return scanning_; }
  bool pairing() const { return pairing_; }
  QString error() const { return error_; }

  Q_INVOKABLE void setRadio(bool on);
  Q_INVOKABLE void connectDevice(const QString& id);
  Q_INVOKABLE void disconnectDevice(const QString& id);
  Q_INVOKABLE void scan();
  Q_INVOKABLE void pairDevice(const QString& id);
  Q_INVOKABLE void forgetDevice(const QString& id);
  Q_INVOKABLE void cancelPair();

  // Test seams: the retry delays (ms) and the observed safety-net interval (ms).
  void setRetrySchedule(const QList<int>& delaysMs) { retryDelays_ = delaysMs; }
  void setSafetyInterval(int ms) { safetyTimer_.setInterval(ms); }
  bool safetyTimerActive() const { return safetyTimer_.isActive(); }

 signals:
  void changed();
  void scanningChanged();
  void pairingChanged();
  void errorChanged();

 protected:
  void connectNotify(const QMetaMethod& signal) override;
  void disconnectNotify(const QMetaMethod& signal) override;

 private:
  bool observed() const;
  void updateSafetyTimer();
  void retryTick();
  QVector<BluetoothDevice> connectedDevices() const;
  void onStatus(const BluetoothStatus& status);
  void onScanCompleted(int status, const QVector<BluetoothDevice>& found);
  void onPairingFinished(bool success, const QString& message, const BluetoothDevice& device);
  void startPairing(bool pair, const QString& id);
  void setScanning(bool scanning);
  void setPairing(bool pairing);
  void setError(const QString& error);

  std::unique_ptr<BluetoothBackend> backend_;
  BluetoothStatus status_;
  QVector<BluetoothDevice> nearby_;  // unpaired devices of the last scan
  bool scanning_ = false;
  bool pairing_ = false;
  QString error_;
  QList<int> retryDelays_{1000, 3000, 8000, 20000};
  int retriesDone_ = 0;
  QTimer retryTimer_;
  QTimer safetyTimer_;
};

}  // namespace wabar::bluetooth
