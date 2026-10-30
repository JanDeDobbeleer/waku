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

// Counters the test sets; `reads` counts the calls.
class FakeThroughputSource final : public ThroughputSource {
 public:
  std::vector<InterfaceCounters> read() override {
    ++reads;
    std::lock_guard lock(mutex);
    return counters;
  }
  void set(std::vector<InterfaceCounters> next) {
    std::lock_guard lock(mutex);
    counters = std::move(next);
  }

  std::atomic<int> reads{0};

 private:
  std::mutex mutex;
  std::vector<InterfaceCounters> counters;
};

// What the service owns: forwards to the fake the test holds.
class ForwardingThroughput final : public ThroughputSource {
 public:
  explicit ForwardingThroughput(FakeThroughputSource* target) : target_(target) {}
  std::vector<InterfaceCounters> read() override { return target_->read(); }

 private:
  FakeThroughputSource* target_;
};

// Records the requests; the test emits the answers.
class FakeWifiRadio final : public WifiRadioBackend {
 public:
  void query() override { ++queries; }
  void setEnabled(bool enabled) override { requests.push_back(enabled); }

  int queries = 0;
  std::vector<bool> requests;
};

class FakeVpn final : public VpnBackend {
 public:
  void refresh() override { ++refreshes; }
  void connectEntry(const QString& name) override { dials.push_back(name); }
  void disconnectEntry(const QString& name) override { hangUps.push_back(name); }

  int refreshes = 0;
  std::vector<QString> dials;
  std::vector<QString> hangUps;
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
  std::unique_ptr<ThroughputSource> createThroughputSource() override {
    if (!withThroughput) return nullptr;
    auto created = std::make_unique<ForwardingThroughput>(&throughput);
    return created;
  }
  std::unique_ptr<WifiRadioBackend> createWifiRadio() override {
    if (!withRadio) return nullptr;
    auto created = std::make_unique<FakeWifiRadio>();
    radio = created.get();
    return created;
  }
  std::unique_ptr<VpnBackend> createVpnBackend() override {
    if (!withVpn) return nullptr;
    auto created = std::make_unique<FakeVpn>();
    vpn = created.get();
    return created;
  }
  void set(NetworkStatus next) {
    std::lock_guard lock(mutex);
    status = std::move(next);
  }

  std::atomic<int> calls{0};
  FakeWifiBackend* backend = nullptr;
  // Shared with the reader the service owns, so a test can change the counters and count reads.
  FakeThroughputSource throughput;
  FakeWifiRadio* radio = nullptr;
  FakeVpn* vpn = nullptr;
  bool withThroughput = true;
  bool withRadio = true;
  bool withVpn = true;

 private:
  std::mutex mutex;
  NetworkStatus status;
};

}  // namespace wabar::network::testing
