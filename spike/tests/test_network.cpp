#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "spike/services/network_service.h"

using namespace wabar::services;
namespace platform = yasb::platform;

namespace {

class FakeWifiBackend final : public platform::WifiBackend {
 public:
  void setUpdatesEnabled(bool enabled) override { updates = enabled; }
  void scan() override { ++scans; }
  void queryCurrent() override {}
  void connectTo(const platform::WifiNetwork& network, const QString& ssid, const QString&,
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
  std::unique_ptr<platform::WifiBackend> createWifiBackend() override {
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

NetworkStatus wifiStatus(int signal = 80, const QString& vpn = {}) {
  NetworkStatus status;
  status.kind = QStringLiteral("wifi");
  status.ssid = QStringLiteral("Home");
  status.signalPercent = signal;
  status.ipAddress = QStringLiteral("192.168.1.5");
  status.vpnName = vpn;
  return status;
}

platform::WifiNetwork scanned(const QString& ssid, int quality, bool secured, bool known,
                              bool connected = false) {
  platform::WifiNetwork network;
  network.ssid = ssid;
  network.quality = quality;
  network.state = (secured ? platform::kWifiSecured : platform::kWifiUnsecured) |
                  (connected ? platform::kWifiConnected : 0);
  network.profileExists = known;
  return network;
}

platform::ConnectivitySample sample(const char* name, int bars, bool ethernet,
                                    const char* vpn = "") {
  platform::ConnectivitySample s;
  s.info.name = name;
  s.info.bars = bars;
  s.info.ethernet = ethernet;
  s.info.vpnName = vpn;
  s.ipAddress = "10.0.0.2";
  return s;
}

bool waitFor(const std::function<bool()>& condition, int timeoutMs = 3000) {
  return QTest::qWaitFor(condition, timeoutMs);
}

}  // namespace

TEST(NetworkStatusMapping, WifiUsesTheExactQualityWhenKnown) {
  const NetworkStatus status = statusFromSample(sample("Home", 3, false), 71);
  EXPECT_EQ(status.kind, QStringLiteral("wifi"));
  EXPECT_EQ(status.ssid, QStringLiteral("Home"));
  EXPECT_EQ(status.signalPercent, 71);
  EXPECT_EQ(status.ipAddress, QStringLiteral("10.0.0.2"));
  EXPECT_TRUE(status.vpnName.isEmpty());
}

TEST(NetworkStatusMapping, WifiFallsBackToTheBars) {
  EXPECT_EQ(statusFromSample(sample("Home", 3, false), std::nullopt).signalPercent, 75);
  EXPECT_EQ(statusFromSample(sample("Home", 5, false), std::nullopt).signalPercent, 100);
}

TEST(NetworkStatusMapping, EthernetHasNoSsidOrSignal) {
  const NetworkStatus status = statusFromSample(sample("Ethernet 2", 0, true), std::nullopt);
  EXPECT_EQ(status.kind, QStringLiteral("ethernet"));
  EXPECT_TRUE(status.ssid.isEmpty());
  EXPECT_EQ(status.signalPercent, 0);
}

TEST(NetworkStatusMapping, DisconnectedAndErrorMeanNone) {
  EXPECT_EQ(statusFromSample(sample("Disconnected", 0, false), std::nullopt).kind,
            QStringLiteral("none"));
  EXPECT_EQ(statusFromSample(sample("Error", 0, false), std::nullopt).kind, QStringLiteral("none"));
}

TEST(NetworkStatusMapping, VpnNameComesThroughOnEveryKind) {
  EXPECT_EQ(statusFromSample(sample("Home", 4, false, "Work VPN"), std::nullopt).vpnName,
            QStringLiteral("Work VPN"));
  EXPECT_EQ(statusFromSample(sample("Ethernet", 0, true, "Work VPN"), std::nullopt).vpnName,
            QStringLiteral("Work VPN"));
}

TEST(NetworkStatusMapping, LookupFailureLeavesTheAddressEmpty) {
  platform::ConnectivitySample s = sample("Home", 4, false);
  s.ipAddress.reset();
  EXPECT_TRUE(statusFromSample(s, std::nullopt).ipAddress.isEmpty());
}

TEST(Network, WithoutASourceItIsUnavailableAndQuiet) {
  Network network(nullptr, nullptr);
  network.componentComplete();
  EXPECT_FALSE(network.available());
  EXPECT_EQ(network.kind(), QStringLiteral("none"));
  EXPECT_FALSE(network.vpnActive());
  network.refresh();
  EXPECT_FALSE(network.scanning());
  EXPECT_FALSE(network.connectTo(QStringLiteral("Home")));
}

TEST(Network, StartsPollingAtComponentComplete) {
  auto fake = std::make_shared<FakeSource>();
  fake->set(wifiStatus());
  Network network(fake, nullptr);
  QTest::qWait(200);
  EXPECT_EQ(fake->calls.load(), 0);

  network.componentComplete();
  ASSERT_TRUE(waitFor([&] { return network.ready(); }));
  EXPECT_TRUE(network.available());
  EXPECT_EQ(network.kind(), QStringLiteral("wifi"));
  EXPECT_EQ(network.ssid(), QStringLiteral("Home"));
  EXPECT_EQ(network.signalPercent(), 80);
  EXPECT_EQ(network.ipAddress(), QStringLiteral("192.168.1.5"));
  EXPECT_FALSE(network.vpnActive());
}

TEST(Network, FollowsTheStateAtEachPoll) {
  auto fake = std::make_shared<FakeSource>();
  fake->set(wifiStatus(80));
  Network network(fake, nullptr);
  network.setInterval(100);
  network.componentComplete();
  ASSERT_TRUE(waitFor([&] { return network.ready(); }));

  fake->set(wifiStatus(35, QStringLiteral("Work VPN")));
  ASSERT_TRUE(waitFor([&] { return network.signalPercent() == 35; }));
  EXPECT_TRUE(network.vpnActive());
  EXPECT_EQ(network.vpnName(), QStringLiteral("Work VPN"));

  NetworkStatus ethernet;
  ethernet.kind = QStringLiteral("ethernet");
  ethernet.ipAddress = QStringLiteral("10.1.1.1");
  fake->set(ethernet);
  ASSERT_TRUE(waitFor([&] { return network.kind() == QStringLiteral("ethernet"); }));
  EXPECT_TRUE(network.ssid().isEmpty());
  EXPECT_FALSE(network.vpnActive());
  EXPECT_EQ(network.ipAddress(), QStringLiteral("10.1.1.1"));
}

TEST(Network, RefreshScansAndListsNetworksConnectedFirstThenBySignal) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  EXPECT_EQ(fake->backend, nullptr);  // created lazily
  QSignalSpy changed(&network, &Network::networksChanged);

  network.refresh();
  ASSERT_NE(fake->backend, nullptr);
  EXPECT_TRUE(fake->backend->updates);
  EXPECT_EQ(fake->backend->scans, 1);
  EXPECT_TRUE(network.scanning());

  emit fake->backend->scanCompleted(static_cast<int>(platform::WifiScanStatus::Success),
                                    {scanned(QStringLiteral("Cafe"), 90, false, false),
                                     scanned(QStringLiteral("Home"), 40, true, true, true),
                                     scanned(QStringLiteral("Hidden"), 10, true, false),
                                     scanned(QStringLiteral(""), 99, true, false),
                                     scanned(QStringLiteral("Cafe"), 50, false, false),
                                     scanned(QStringLiteral("Work"), 60, true, true)});

  EXPECT_FALSE(network.scanning());
  const QVariantList list = network.networks();
  ASSERT_EQ(list.size(), 4);
  EXPECT_EQ(list[0].toMap().value("ssid").toString(), QStringLiteral("Home"));
  EXPECT_TRUE(list[0].toMap().value("connected").toBool());
  EXPECT_TRUE(list[0].toMap().value("secured").toBool());
  EXPECT_TRUE(list[0].toMap().value("known").toBool());
  EXPECT_EQ(list[1].toMap().value("ssid").toString(), QStringLiteral("Cafe"));
  EXPECT_EQ(list[1].toMap().value("signal").toInt(), 90);
  EXPECT_FALSE(list[1].toMap().value("secured").toBool());
  EXPECT_EQ(list[2].toMap().value("ssid").toString(), QStringLiteral("Work"));
  EXPECT_EQ(list[3].toMap().value("ssid").toString(), QStringLiteral("Hidden"));
  EXPECT_GE(changed.count(), 2);
}

TEST(Network, AFailedScanEmptiesTheList) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  network.refresh();
  emit fake->backend->scanCompleted(static_cast<int>(platform::WifiScanStatus::Success),
                                    {scanned(QStringLiteral("Home"), 40, true, true)});
  ASSERT_EQ(network.networks().size(), 1);
  network.refresh();
  emit fake->backend->scanCompleted(static_cast<int>(platform::WifiScanStatus::AccessDenied), {});
  EXPECT_TRUE(network.networks().isEmpty());
  EXPECT_FALSE(network.scanning());
}

TEST(Network, ConnectsOnlyToKnownScannedNetworks) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  network.refresh();
  emit fake->backend->scanCompleted(static_cast<int>(platform::WifiScanStatus::Success),
                                    {scanned(QStringLiteral("Work"), 60, true, true),
                                     scanned(QStringLiteral("Cafe"), 90, true, false)});

  EXPECT_FALSE(network.connectTo(QStringLiteral("Cafe")));     // needs a password
  EXPECT_FALSE(network.connectTo(QStringLiteral("Nowhere")));  // not in the scan
  EXPECT_TRUE(fake->backend->connects.empty());

  EXPECT_TRUE(network.connectTo(QStringLiteral("Work")));
  ASSERT_EQ(fake->backend->connects.size(), 1u);
  EXPECT_EQ(fake->backend->connects[0].ssid, QStringLiteral("Work"));
  EXPECT_FALSE(fake->backend->connects[0].hidden);
  EXPECT_FALSE(network.connectTo(QStringLiteral("Work")));  // one connect at a time
}

TEST(Network, AFinishedConnectScansAndPollsAgain) {
  auto fake = std::make_shared<FakeSource>();
  fake->set(wifiStatus());
  Network network(fake, nullptr);
  network.refresh();
  emit fake->backend->scanCompleted(static_cast<int>(platform::WifiScanStatus::Success),
                                    {scanned(QStringLiteral("Work"), 60, true, true)});
  ASSERT_TRUE(network.connectTo(QStringLiteral("Work")));

  emit fake->backend->connectFinished(static_cast<int>(platform::WifiConnectStatus::Success),
                                      QStringLiteral("Work"),
                                      scanned(QStringLiteral("Work"), 60, true, true, true));
  EXPECT_EQ(fake->backend->scans, 2);
  ASSERT_TRUE(waitFor([&] { return network.ready(); }));
  EXPECT_TRUE(network.connectTo(QStringLiteral("Work")));  // the connect is over
}

TEST(Network, AFailedConnectAllowsANewOne) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  network.refresh();
  emit fake->backend->scanCompleted(static_cast<int>(platform::WifiScanStatus::Success),
                                    {scanned(QStringLiteral("Work"), 60, true, true)});
  ASSERT_TRUE(network.connectTo(QStringLiteral("Work")));
  emit fake->backend->connectFailed(QStringLiteral("no"));
  EXPECT_TRUE(network.connectTo(QStringLiteral("Work")));
}
