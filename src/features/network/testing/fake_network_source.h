#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "features/network/qml/network_service.h"

namespace waku::network::testing {

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

// The test emits `changed` through notify().
class FakeChangeWatcher final : public NetworkChangeWatcher {
 public:
  void notify() { emit changed(); }
};

class FakeSource final : public NetworkSource {
 public:
  NetworkStatus sample() override {
    ++calls;
    NetworkStatus read;
    {
      std::lock_guard lock(mutex);
      read = status;  // what the state was when the sample started
    }
    ++snapshots;
    while (block) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return read;
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
  std::unique_ptr<NetworkChangeWatcher> createChangeWatcher() override {
    if (!withWatcher) return nullptr;
    auto created = std::make_unique<FakeChangeWatcher>();
    watcher = created.get();
    return created;
  }
  void set(NetworkStatus next) {
    std::lock_guard lock(mutex);
    status = std::move(next);
  }

  std::atomic<int> calls{0};
  // While true, sample() waits: a test holds a sample in flight.
  std::atomic<bool> block{false};
  // Counts the samples that have read the state (they may still be held by `block`).
  std::atomic<int> snapshots{0};
  FakeWifiBackend* backend = nullptr;
  // Shared with the reader the service owns, so a test can change the counters and count reads.
  FakeThroughputSource throughput;
  FakeWifiRadio* radio = nullptr;
  FakeVpn* vpn = nullptr;
  FakeChangeWatcher* watcher = nullptr;
  bool withWatcher = true;
  bool withThroughput = true;
  bool withRadio = true;
  bool withVpn = true;

 private:
  std::mutex mutex;
  NetworkStatus status;
};

}  // namespace waku::network::testing
