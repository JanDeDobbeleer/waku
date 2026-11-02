#include <gtest/gtest.h>

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "features/network/qml/network_service.h"
#include "features/network/testing/fake_network_source.h"

using namespace waku::network;
using namespace waku::network::testing;

namespace {

NetworkStatus wifiStatus(int signal = 80, const QString& vpn = {}) {
  NetworkStatus status;
  status.kind = QStringLiteral("wifi");
  status.ssid = QStringLiteral("Home");
  status.signalPercent = signal;
  status.ipAddress = QStringLiteral("192.168.1.5");
  status.vpnName = vpn;
  return status;
}

WifiNetwork scanned(const QString& ssid, int quality, bool secured, bool known,
                    bool connected = false) {
  WifiNetwork network;
  network.ssid = ssid;
  network.quality = quality;
  network.state = (secured ? kWifiSecured : kWifiUnsecured) | (connected ? kWifiConnected : 0);
  network.profileExists = known;
  return network;
}

ConnectivitySample sample(const char* name, int bars, bool ethernet, const char* vpn = "") {
  ConnectivitySample s;
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

// Something connected to `wifiChanged` and `vpnChanged`, which is what makes the service read the
// radio and the VPN list.
struct Observer {
  explicit Observer(Network& network) {
    QObject::connect(&network, &Network::wifiChanged, &context, [] {});
    QObject::connect(&network, &Network::vpnChanged, &context, [] {});
  }
  QObject context;
};

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
  ConnectivitySample s = sample("Home", 4, false);
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
  // No timer and no sample on the pool: nothing can run later either.
  EXPECT_FALSE(network.polling());
  EXPECT_FALSE(network.sampling());
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

  emit fake->backend->scanCompleted(static_cast<int>(WifiScanStatus::Success),
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
  emit fake->backend->scanCompleted(static_cast<int>(WifiScanStatus::Success),
                                    {scanned(QStringLiteral("Home"), 40, true, true)});
  ASSERT_EQ(network.networks().size(), 1);
  network.refresh();
  emit fake->backend->scanCompleted(static_cast<int>(WifiScanStatus::AccessDenied), {});
  EXPECT_TRUE(network.networks().isEmpty());
  EXPECT_FALSE(network.scanning());
}

TEST(Network, ConnectsOnlyToKnownScannedNetworks) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  network.refresh();
  emit fake->backend->scanCompleted(static_cast<int>(WifiScanStatus::Success),
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
  emit fake->backend->scanCompleted(static_cast<int>(WifiScanStatus::Success),
                                    {scanned(QStringLiteral("Work"), 60, true, true)});
  ASSERT_TRUE(network.connectTo(QStringLiteral("Work")));

  emit fake->backend->connectFinished(static_cast<int>(WifiConnectStatus::Success),
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
  emit fake->backend->scanCompleted(static_cast<int>(WifiScanStatus::Success),
                                    {scanned(QStringLiteral("Work"), 60, true, true)});
  ASSERT_TRUE(network.connectTo(QStringLiteral("Work")));
  emit fake->backend->connectFailed(QStringLiteral("no"));
  EXPECT_TRUE(network.connectTo(QStringLiteral("Work")));
}

namespace {

InterfaceCounters counters(quint64 id, quint64 rx, quint64 tx, bool up = true) {
  InterfaceCounters c;
  c.id = id;
  c.name = QStringLiteral("Ethernet");
  c.isUp = up;
  c.rxBytes = rx;
  c.txBytes = tx;
  return c;
}

}  // namespace

TEST(NetworkThroughput, ReadsNothingWhileUnobserved) {
  auto fake = std::make_shared<FakeSource>();
  fake->throughput.set({counters(1, 0, 0)});
  Network network(fake, nullptr);
  network.setThroughputInterval(20);
  network.componentComplete();
  EXPECT_FALSE(network.throughputSampling());  // nobody connected: no read can come
  EXPECT_EQ(fake->throughput.reads.load(), 0);
  EXPECT_EQ(network.rxBytesPerSecond(), 0);
  EXPECT_TRUE(network.adapters().isEmpty());
}

TEST(NetworkThroughput, SamplesWhileSomethingIsConnectedAndStopsAfter) {
  auto fake = std::make_shared<FakeSource>();
  fake->throughput.set({counters(1, 1000, 1000)});
  Network network(fake, nullptr);
  network.setThroughputInterval(20);

  QObject context;
  const QMetaObject::Connection connection =
      QObject::connect(&network, &Network::throughputChanged, &context, [] {});
  EXPECT_EQ(fake->throughput.reads.load(), 1);  // the baseline

  fake->throughput.set({counters(1, 51000, 21000)});
  ASSERT_TRUE(waitFor([&] { return network.rxBytesPerSecond() > 0; }));
  EXPECT_GT(network.txBytesPerSecond(), 0);
  EXPECT_DOUBLE_EQ(network.rxBytesTotal(), 50000);
  EXPECT_DOUBLE_EQ(network.txBytesTotal(), 20000);
  ASSERT_EQ(network.adapters().size(), 1);
  EXPECT_EQ(network.adapters()[0].toMap().value("name").toString(), QStringLiteral("Ethernet"));
  EXPECT_TRUE(network.adapters()[0].toMap().value("isUp").toBool());

  QObject::disconnect(connection);
  EXPECT_TRUE(network.throughputSampling());  // the check is queued, see disconnectNotify
  ASSERT_TRUE(waitFor([&] { return !network.throughputSampling(); }));
  const int reads = fake->throughput.reads.load();
  QCoreApplication::processEvents();
  EXPECT_FALSE(network.throughputSampling());
  EXPECT_EQ(fake->throughput.reads.load(), reads);
  EXPECT_EQ(network.rxBytesPerSecond(), 0);
  EXPECT_DOUBLE_EQ(network.rxBytesTotal(), 50000);  // the total survives the pause
}

TEST(NetworkThroughput, ACounterResetAndAVanishedInterfaceStaySane) {
  auto fake = std::make_shared<FakeSource>();
  fake->throughput.set({counters(1, 900000, 0), counters(2, 500, 0)});
  Network network(fake, nullptr);
  network.setThroughputInterval(20);
  QObject context;
  QObject::connect(&network, &Network::throughputChanged, &context, [] {});

  fake->throughput.set({counters(1, 100, 0)});  // 1 was reset, 2 vanished
  ASSERT_TRUE(waitFor([&] { return network.adapters().size() == 1; }));
  EXPECT_DOUBLE_EQ(network.rxBytesTotal(), 100);
  EXPECT_GT(network.rxBytesPerSecond(), 0);
}

TEST(NetworkThroughput, WithoutAReaderOrASourceItStaysZero) {
  auto fake = std::make_shared<FakeSource>();
  fake->withThroughput = false;
  Network network(fake, nullptr);
  QObject context;
  QObject::connect(&network, &Network::throughputChanged, &context, [] {});
  Network bare(nullptr, nullptr);
  QObject::connect(&bare, &Network::throughputChanged, &context, [] {});
  EXPECT_FALSE(network.throughputSampling());  // no counter reader: the sampler never starts
  EXPECT_FALSE(bare.throughputSampling());     // no source at all
  EXPECT_EQ(network.rxBytesPerSecond(), 0);
  EXPECT_EQ(bare.rxBytesPerSecond(), 0);
}

TEST(NetworkObservers, UnobservedRadioAndVpnAreNeverRead) {
  auto fake = std::make_shared<FakeSource>();
  fake->set(wifiStatus());
  Network network(fake, nullptr);
  network.setInterval(100);
  network.componentComplete();
  ASSERT_TRUE(waitFor([&] { return fake->calls.load() >= 4; }));
  EXPECT_EQ(fake->radio, nullptr);  // not even created
  EXPECT_EQ(fake->vpn, nullptr);
  EXPECT_FALSE(network.radioObserved());
  EXPECT_FALSE(network.vpnObserved());
}

TEST(NetworkObservers, ObservedRadioAndVpnAreReadAtEveryPoll) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  Observer observer(network);
  network.setInterval(100);
  network.componentComplete();
  ASSERT_TRUE(waitFor([&] { return fake->radio && fake->vpn; }));
  ASSERT_TRUE(waitFor([&] { return fake->radio->queries >= 3 && fake->vpn->refreshes >= 3; }));
}

TEST(NetworkObservers, TheFirstObserverReadsAtOnceNotAtTheNextPoll) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  network.componentComplete();  // polls every 60 s: nothing else is coming soon
  ASSERT_TRUE(waitFor([&] { return network.ready(); }));
  ASSERT_EQ(fake->radio, nullptr);
  ASSERT_EQ(fake->vpn, nullptr);

  QObject context;
  QObject::connect(&network, &Network::wifiChanged, &context, [] {});
  ASSERT_TRUE(waitFor([&] { return fake->radio && fake->radio->queries >= 1; }));
  EXPECT_EQ(fake->vpn, nullptr);  // only what is observed
  QObject::connect(&network, &Network::vpnChanged, &context, [] {});
  ASSERT_TRUE(waitFor([&] { return fake->vpn && fake->vpn->refreshes >= 1; }));
}

// A QML binding and a Connections object count as observers (QML does not use QObject::connect;
// Qt tells the object through connectNotify and the signal mask).
TEST(NetworkObservers, QmlBindingsAndConnectionsObserve) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  network.componentComplete();
  ASSERT_TRUE(waitFor([&] { return network.ready(); }));
  ASSERT_FALSE(network.radioObserved());
  ASSERT_FALSE(network.vpnObserved());

  QQmlEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("net"), &network);
  QQmlComponent component(&engine);
  component.setData(R"(
    import QtQml
    QtObject {
      property bool on: net.wifiEnabled
      property Connections follow: Connections {
        target: net
        function onVpnChanged() {}
      }
    }
  )",
                    QUrl());
  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << component.errorString().toStdString();
  EXPECT_TRUE(network.radioObserved());
  EXPECT_TRUE(network.vpnObserved());
  ASSERT_TRUE(waitFor([&] { return fake->radio && fake->radio->queries >= 1; }));
  ASSERT_TRUE(waitFor([&] { return fake->vpn && fake->vpn->refreshes >= 1; }));
}

TEST(NetworkChanges, ANotificationSamplesOnceAfterTheDebounce) {
  auto fake = std::make_shared<FakeSource>();
  fake->set(wifiStatus(80));
  Network network(fake, nullptr);
  network.setChangeDebounce(50);
  network.componentComplete();
  ASSERT_TRUE(waitFor([&] { return network.ready(); }));
  ASSERT_NE(fake->watcher, nullptr);
  EXPECT_EQ(network.interval(), 60000);  // the timer is only the safety net
  EXPECT_EQ(fake->calls.load(), 1);

  fake->set(wifiStatus(35));
  for (int i = 0; i < 5; ++i) fake->watcher->notify();  // a burst
  ASSERT_TRUE(waitFor([&] { return network.signalPercent() == 35; }));
  QTest::qWait(200);
  EXPECT_EQ(fake->calls.load(), 2);
}

TEST(NetworkChanges, ANotificationDuringASampleSamplesAgainAfterIt) {
  auto fake = std::make_shared<FakeSource>();
  fake->set(wifiStatus(80));
  Network network(fake, nullptr);
  network.setChangeDebounce(20);
  network.componentComplete();
  ASSERT_TRUE(waitFor([&] { return network.ready(); }));

  fake->block = true;
  fake->watcher->notify();
  ASSERT_TRUE(waitFor([&] { return fake->snapshots.load() >= 2; }));  // held, state read
  fake->set(wifiStatus(35));  // the change the held sample missed
  fake->watcher->notify();
  QTest::qWait(100);  // the debounce ended while the sample ran
  fake->block = false;
  ASSERT_TRUE(waitFor([&] { return network.signalPercent() == 35; }));
  EXPECT_EQ(fake->calls.load(), 3);  // the one that ran, and the one queued behind it
}

TEST(NetworkChanges, WithoutAWatcherThePollFallsBackToFiveSeconds) {
  auto fake = std::make_shared<FakeSource>();
  fake->withWatcher = false;
  Network network(fake, nullptr);
  network.componentComplete();
  EXPECT_EQ(network.interval(), 5000);

  auto watched = std::make_shared<FakeSource>();
  Network other(watched, nullptr);
  other.componentComplete();
  EXPECT_EQ(other.interval(), 60000);
}

TEST(NetworkChanges, APausedServiceIgnoresNotifications) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  network.setChangeDebounce(20);
  network.componentComplete();
  ASSERT_TRUE(waitFor([&] { return network.ready(); }));
  network.setActive(false);
  const int calls = fake->calls.load();
  fake->watcher->notify();
  QTest::qWait(150);
  EXPECT_EQ(fake->calls.load(), calls);
}

TEST(NetworkWifiRadio, FollowsTheStateWindowsReports) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  Observer observer(network);
  network.componentComplete();
  ASSERT_NE(fake->radio, nullptr);
  EXPECT_GE(fake->radio->queries, 1);
  EXPECT_FALSE(network.wifiAvailable());
  EXPECT_FALSE(network.wifiEnabled());

  emit fake->radio->stateRead(true, true);
  EXPECT_TRUE(network.wifiAvailable());
  EXPECT_TRUE(network.wifiEnabled());
  emit fake->radio->stateRead(false, true);  // unreadable: never "on"
  EXPECT_FALSE(network.wifiAvailable());
  EXPECT_FALSE(network.wifiEnabled());
}

TEST(NetworkWifiRadio, ASwitchFailureLeavesTheRealStateAndReportsTheError) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  Observer observer(network);
  network.componentComplete();
  emit fake->radio->stateRead(true, true);
  QSignalSpy changed(&network, &Network::wifiChanged);

  EXPECT_TRUE(network.setWifiEnabled(false));
  EXPECT_TRUE(network.wifiSwitching());
  ASSERT_EQ(fake->radio->requests.size(), 1u);
  EXPECT_FALSE(fake->radio->requests[0]);
  EXPECT_FALSE(network.setWifiEnabled(true));  // one switch at a time
  EXPECT_EQ(fake->radio->requests.size(), 1u);

  emit fake->radio->switchFinished(false, QStringLiteral("Access denied"));
  EXPECT_FALSE(network.wifiSwitching());
  EXPECT_EQ(network.wifiSwitchError(), QStringLiteral("Access denied"));
  EXPECT_TRUE(network.wifiEnabled());  // still what Windows says
  EXPECT_GE(changed.count(), 2);

  EXPECT_TRUE(network.setWifiEnabled(false));
  EXPECT_TRUE(network.wifiSwitchError().isEmpty());
  emit fake->radio->switchFinished(true, QString());
  emit fake->radio->stateRead(true, false);
  EXPECT_TRUE(network.wifiSwitchError().isEmpty());
  EXPECT_FALSE(network.wifiEnabled());
}

TEST(NetworkWifiRadio, WithoutARadioBackendTheSwitchFailsSoft) {
  auto fake = std::make_shared<FakeSource>();
  fake->withRadio = false;
  Network network(fake, nullptr);
  Observer observer(network);
  EXPECT_FALSE(network.setWifiEnabled(true));
  EXPECT_FALSE(network.wifiSwitchError().isEmpty());
  EXPECT_FALSE(network.wifiSwitching());

  Network bare(nullptr, nullptr);
  EXPECT_FALSE(bare.setWifiEnabled(true));
}

TEST(NetworkVpn, ListsTheConfiguredEntriesAndTheirState) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  Observer observer(network);
  network.componentComplete();
  ASSERT_NE(fake->vpn, nullptr);
  EXPECT_GE(fake->vpn->refreshes, 1);
  EXPECT_TRUE(network.vpnConnections().isEmpty());

  QSignalSpy changed(&network, &Network::vpnChanged);
  emit fake->vpn->entriesRead({{QStringLiteral("Work"), false}, {QStringLiteral("Home"), true}});
  ASSERT_EQ(network.vpnConnections().size(), 2);
  EXPECT_EQ(network.vpnConnections()[0].toMap().value("name").toString(), QStringLiteral("Work"));
  EXPECT_FALSE(network.vpnConnections()[0].toMap().value("connected").toBool());
  EXPECT_TRUE(network.vpnConnections()[1].toMap().value("connected").toBool());
  EXPECT_EQ(changed.count(), 1);
  emit fake->vpn->entriesRead({{QStringLiteral("Work"), false}, {QStringLiteral("Home"), true}});
  EXPECT_EQ(changed.count(), 1);  // unchanged lists are not announced
}

TEST(NetworkVpn, DialsAndHangsUpKnownEntriesOneAtATime) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  Observer observer(network);
  network.componentComplete();
  emit fake->vpn->entriesRead({{QStringLiteral("Work"), false}, {QStringLiteral("Home"), true}});

  EXPECT_TRUE(network.connectVpn(QStringLiteral("Work")));
  EXPECT_TRUE(network.vpnBusy());
  ASSERT_EQ(fake->vpn->dials.size(), 1u);
  EXPECT_EQ(fake->vpn->dials[0], QStringLiteral("Work"));
  EXPECT_FALSE(network.disconnectVpn(QStringLiteral("Home")));  // busy

  const int refreshes = fake->vpn->refreshes;
  emit fake->vpn->operationFinished(QStringLiteral("Work"), false, QStringLiteral("Dial failed"));
  EXPECT_FALSE(network.vpnBusy());
  EXPECT_EQ(network.vpnError(), QStringLiteral("Dial failed"));
  EXPECT_GT(fake->vpn->refreshes, refreshes);

  EXPECT_TRUE(network.disconnectVpn(QStringLiteral("Home")));
  EXPECT_TRUE(network.vpnError().isEmpty());
  ASSERT_EQ(fake->vpn->hangUps.size(), 1u);
  EXPECT_EQ(fake->vpn->hangUps[0], QStringLiteral("Home"));
  emit fake->vpn->operationFinished(QStringLiteral("Home"), true, QString());
  EXPECT_FALSE(network.vpnBusy());
  EXPECT_TRUE(network.vpnError().isEmpty());
}

TEST(NetworkVpn, RefusesUnknownEntriesAndRedundantRequests) {
  auto fake = std::make_shared<FakeSource>();
  Network network(fake, nullptr);
  Observer observer(network);
  network.componentComplete();
  emit fake->vpn->entriesRead({{QStringLiteral("Work"), false}, {QStringLiteral("Home"), true}});

  EXPECT_FALSE(network.connectVpn(QStringLiteral("Nope")));
  EXPECT_FALSE(network.vpnError().isEmpty());
  EXPECT_FALSE(network.connectVpn(QStringLiteral("Home")));     // already connected
  EXPECT_FALSE(network.disconnectVpn(QStringLiteral("Work")));  // not connected
  EXPECT_TRUE(fake->vpn->dials.empty());
  EXPECT_TRUE(fake->vpn->hangUps.empty());
  EXPECT_FALSE(network.vpnBusy());
}

TEST(NetworkVpn, WithoutABackendItFailsSoft) {
  auto fake = std::make_shared<FakeSource>();
  fake->withVpn = false;
  Network network(fake, nullptr);
  Observer observer(network);
  network.componentComplete();
  EXPECT_FALSE(network.connectVpn(QStringLiteral("Work")));
  EXPECT_FALSE(network.vpnError().isEmpty());
  Network bare(nullptr, nullptr);
  EXPECT_FALSE(bare.disconnectVpn(QStringLiteral("Work")));
}
