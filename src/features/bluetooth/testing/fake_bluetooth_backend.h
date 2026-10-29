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
  void scan() override {}
  bool setRadio(bool on) override {
    radioRequests.push_back(on);
    return true;
  }
  void connectDevice(const QString& address) override { connects.push_back(address); }
  void disconnectDevice(const QString& address) override { disconnects.push_back(address); }
  void publish(const BluetoothStatus& status) { publishStatus(status); }

  int starts = 0;
  int refreshes = 0;
  std::vector<bool> radioRequests;
  QStringList connects;
  QStringList disconnects;
};

}  // namespace wabar::bluetooth
