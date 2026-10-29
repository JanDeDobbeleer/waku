#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "features/network/qml/network_service.h"

namespace wabar::network::testing {

class FakeWifiBackend final : public WifiBackend {
 public:
  void setUpdatesEnabled(bool enabled) override { updates = enabled; }
  void scan() override { ++scans; }
  void queryCurrent() override {}
  void connectTo(const WifiNetwork& network, const QString& ssid, const QString&,
                 bool hidden) override {
    connects.push_back({network.ssid, ssid, hidden});
  }
  void disconnectCurrent() override {}
  void forget(const QString&) override {}
  void setAutoConnect(const QString&, bool) override {}

  struct Connect {
    QString network;
    QString ssid;
    bool hidden;
  };
  bool updates = false;
  int scans = 0;
  std::vector<Connect> connects;
};

class FakeSource final : public NetworkSource {
 public:
  NetworkStatus sample() override {
    ++calls;
    std::lock_guard lock(mutex);
    return status;
  }
  std::unique_ptr<WifiBackend> createWifiBackend() override {
    auto created = std::make_unique<FakeWifiBackend>();
    backend = created.get();
    return created;
  }
  void set(NetworkStatus next) {
    std::lock_guard lock(mutex);
    status = std::move(next);
  }

  std::atomic<int> calls{0};
  FakeWifiBackend* backend = nullptr;

 private:
  std::mutex mutex;
  NetworkStatus status;
};

}  // namespace wabar::network::testing
