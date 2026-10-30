#pragma once

#include <QString>
#include <QStringList>
#include <vector>

#include "features/bluetooth/bluetooth.h"

namespace wabar::bluetooth {

// Test double of BluetoothBackend: records commands, publishes statuses on demand.
class FakeBackend final : public BluetoothBackend {
 public:
  void start() override { ++starts; }
  void refresh() override { ++refreshes; }
  void scan() override { ++scans; }
  bool setRadio(bool on) override {
    radioRequests.push_back(on);
    return true;
  }
  void connectDevice(const QString& address) override { connects.push_back(address); }
  void disconnectDevice(const QString& address) override { disconnects.push_back(address); }
  bool pairDevice(const QString& address) override {
    pairs.push_back(address);
    return !busy;
  }
  bool unpairDevice(const QString& address) override {
    unpairs.push_back(address);
    return !busy;
  }
  void cancelPair() override { ++cancels; }
  void publish(const BluetoothStatus& status) { publishStatus(status); }
  void finishScan(BtScanStatus status, const QVector<BluetoothDevice>& found) {
    emit scanCompleted(static_cast<int>(status), found);
  }
  void finishPairing(bool success, const QString& message, const BluetoothDevice& device) {
    emit pairingFinished(success, message, device);
  }

  int starts = 0;
  int refreshes = 0;
  int scans = 0;
  int cancels = 0;
  bool busy = false;  // pairDevice and unpairDevice return false
  std::vector<bool> radioRequests;
  QStringList connects;
  QStringList disconnects;
  QStringList pairs;
  QStringList unpairs;
};

}  // namespace wabar::bluetooth
