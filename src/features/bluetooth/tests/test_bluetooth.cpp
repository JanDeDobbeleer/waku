#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <memory>

#include "features/bluetooth/qml/bluetooth_service.h"
#include "features/bluetooth/testing/fake_bluetooth_backend.h"

using namespace waku::bluetooth;

namespace {

BluetoothDevice device(const QString& name, bool connected, bool paired = true,
                       std::optional<int> battery = std::nullopt,
                       BtDeviceType type = BtDeviceType::Headphones) {
  BluetoothDevice d;
  d.name = name;
  d.address = QStringLiteral("addr-") + name;
  d.connected = connected;
  d.paired = paired;
  d.battery = battery;
  d.deviceType = type;
  return d;
}

struct Fixture {
  Fixture() {
    auto owned = std::make_unique<FakeBackend>();
    fake = owned.get();
    service = std::make_unique<Bluetooth>(std::move(owned), nullptr);
  }
  FakeBackend* fake = nullptr;
  std::unique_ptr<Bluetooth> service;
};

void wait(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

BluetoothStatus unknownStatus() {
  BluetoothStatus status;
  status.radioUnknown = true;
  return status;
}

BluetoothStatus knownStatus() {
  BluetoothStatus status;
  status.radioOn = true;
  status.devices = {device(QStringLiteral("Buds"), true, true, 80)};
  return status;
}

}  // namespace

TEST(BluetoothService, StartsTheBackendAndIsOffUntilAStatusArrives) {
  Fixture f;
  EXPECT_TRUE(f.service->available());
  EXPECT_EQ(f.fake->starts, 1);
  EXPECT_FALSE(f.service->radioOn());
  EXPECT_EQ(f.service->connectedCount(), 0);
  EXPECT_TRUE(f.service->devices().isEmpty());
}

TEST(BluetoothService, WithoutABackendItIsUnavailableAndInert) {
  Bluetooth service(nullptr, nullptr);
  EXPECT_FALSE(service.available());
  service.setRadio(true);
  service.connectDevice(QStringLiteral("x"));
  service.disconnectDevice(QStringLiteral("x"));
  EXPECT_EQ(service.connectedCount(), 0);
}

TEST(BluetoothService, MirrorsStatusAndNotifies) {
  Fixture f;
  QSignalSpy changed(f.service.get(), &Bluetooth::changed);
  BluetoothStatus status;
  status.radioOn = true;
  status.devices = {device(QStringLiteral("Mouse"), false, true, std::nullopt, BtDeviceType::Mouse),
                    device(QStringLiteral("Buds"), true, true, 80)};
  f.fake->publish(status);
  EXPECT_EQ(changed.count(), 1);
  EXPECT_TRUE(f.service->radioOn());
  EXPECT_EQ(f.service->connectedCount(), 1);
  EXPECT_EQ(f.service->connectedNames(), QStringList{QStringLiteral("Buds")});

  const QVariantList devices = f.service->devices();
  ASSERT_EQ(devices.size(), 2);
  const QVariantMap first = devices[0].toMap();  // connected first
  EXPECT_EQ(first.value(QStringLiteral("name")).toString(), QStringLiteral("Buds"));
  EXPECT_EQ(first.value(QStringLiteral("id")).toString(), QStringLiteral("addr-Buds"));
  EXPECT_TRUE(first.value(QStringLiteral("connected")).toBool());
  EXPECT_EQ(first.value(QStringLiteral("batteryPercent")).toInt(), 80);
  EXPECT_TRUE(first.value(QStringLiteral("canConnect")).toBool());
  const QVariantMap second = devices[1].toMap();
  EXPECT_EQ(second.value(QStringLiteral("batteryPercent")).toInt(), -1);
  EXPECT_FALSE(second.value(QStringLiteral("canConnect")).toBool());
}

TEST(BluetoothService, RadioOffHidesConnectedDevices) {
  Fixture f;
  BluetoothStatus status;
  status.radioOn = false;
  status.devices = {device(QStringLiteral("Buds"), true)};
  f.fake->publish(status);
  EXPECT_EQ(f.service->connectedCount(), 0);
  EXPECT_TRUE(f.service->connectedNames().isEmpty());
}

TEST(BluetoothService, UnpairedDevicesAreNotListedOrCounted) {
  Fixture f;
  BluetoothStatus status;
  status.radioOn = true;
  status.devices = {device(QStringLiteral("Stranger"), true, false),
                    device(QStringLiteral("Buds"), false)};
  f.fake->publish(status);
  EXPECT_EQ(f.service->connectedCount(), 0);
  ASSERT_EQ(f.service->devices().size(), 1);
  EXPECT_EQ(f.service->devices()[0].toMap().value(QStringLiteral("name")).toString(),
            QStringLiteral("Buds"));
}

TEST(BluetoothService, CommandsReachTheBackend) {
  Fixture f;
  f.service->setRadio(true);
  f.service->setRadio(false);
  f.service->connectDevice(QStringLiteral("a"));
  f.service->disconnectDevice(QStringLiteral("b"));
  EXPECT_EQ(f.fake->radioRequests, (std::vector<bool>{true, false}));
  EXPECT_EQ(f.fake->connects, QStringList{QStringLiteral("a")});
  EXPECT_EQ(f.fake->disconnects, QStringList{QStringLiteral("b")});
}

TEST(BluetoothService, AFinishedConnectionRefreshesTheList) {
  Fixture f;
  emit f.fake->connectionFinished(true, QString(), BluetoothDevice());
  EXPECT_EQ(f.fake->refreshes, 1);
}

TEST(BluetoothService, AConnectionIsPendingUntilTheBackendReportsIt) {
  Fixture f;
  BluetoothStatus status;
  status.radioOn = true;
  status.devices = {device(QStringLiteral("Buds"), false)};
  f.fake->publish(status);
  const QString id = QStringLiteral("addr-Buds");
  auto row = [&] { return f.service->devices().at(0).toMap(); };
  EXPECT_FALSE(row().value(QStringLiteral("connecting")).toBool());

  QSignalSpy changed(f.service.get(), &Bluetooth::changed);
  f.service->connectDevice(id);
  EXPECT_TRUE(row().value(QStringLiteral("connecting")).toBool());
  EXPECT_FALSE(row().value(QStringLiteral("disconnecting")).toBool());
  EXPECT_FALSE(row().value(QStringLiteral("connected")).toBool());
  EXPECT_GE(changed.count(), 1);
  // A second request for the same device while one runs does nothing.
  f.service->connectDevice(id);
  EXPECT_EQ(f.fake->connects.size(), 1);

  emit f.fake->connectionFinished(true, QString(), status.devices.front());
  EXPECT_FALSE(row().value(QStringLiteral("connecting")).toBool());
  EXPECT_TRUE(f.service->error().isEmpty());
}

TEST(BluetoothService, AFailedConnectionSetsTheErrorAndClearsThePendingFlag) {
  Fixture f;
  BluetoothStatus status;
  status.radioOn = true;
  status.devices = {device(QStringLiteral("Buds"), true)};
  f.fake->publish(status);
  const QString id = QStringLiteral("addr-Buds");

  f.service->disconnectDevice(id);
  EXPECT_TRUE(f.service->devices().at(0).toMap().value(QStringLiteral("disconnecting")).toBool());
  const int refreshes = f.fake->refreshes;
  QSignalSpy errors(f.service.get(), &Bluetooth::errorChanged);
  emit f.fake->connectionFinished(false, QStringLiteral("Failed to disconnect device"),
                                  status.devices.front());
  EXPECT_FALSE(f.service->devices().at(0).toMap().value(QStringLiteral("disconnecting")).toBool());
  EXPECT_TRUE(f.service->devices().at(0).toMap().value(QStringLiteral("connected")).toBool());
  EXPECT_EQ(f.service->error(), QStringLiteral("Failed to disconnect device"));
  EXPECT_EQ(errors.count(), 1);
  EXPECT_EQ(f.fake->refreshes, refreshes + 1);

  // The next request clears the error.
  f.service->disconnectDevice(id);
  EXPECT_TRUE(f.service->error().isEmpty());
}

namespace {
QStringList& captured() {
  static QStringList lines;
  return lines;
}
QtMessageHandler g_before = nullptr;
void capture(QtMsgType type, const QMessageLogContext& context, const QString& text) {
  if (text.startsWith(QLatin1String("Bluetooth:"))) {
    captured() << text;
    return;
  }
  if (g_before) g_before(type, context, text);
}
}  // namespace

TEST(BluetoothService, EveryRequestAndItsResultIsLogged) {
  captured().clear();
  g_before = qInstallMessageHandler(capture);
  {
    Fixture f;
    BluetoothStatus status;
    status.radioOn = true;
    status.devices = {device(QStringLiteral("Buds"), false)};
    f.fake->publish(status);
    const QString id = QStringLiteral("addr-Buds");
    f.service->connectDevice(id);
    emit f.fake->connectionFinished(false, QStringLiteral("Failed to connect device. Is it on?"),
                                    status.devices.front());
    f.service->connectDevice(id);
    emit f.fake->connectionFinished(true, QString(), status.devices.front());
  }
  qInstallMessageHandler(g_before);
  ASSERT_EQ(captured().size(), 4) << captured().join(QLatin1Char('\n')).toStdString();
  EXPECT_EQ(captured().at(0), QStringLiteral("Bluetooth: connect requested for addr-Buds"));
  EXPECT_EQ(captured().at(1),
            QStringLiteral("Bluetooth: connect Buds (addr-Buds) failed: Failed to "
                           "connect device. Is it on?"));
  EXPECT_EQ(captured().at(3), QStringLiteral("Bluetooth: connect Buds (addr-Buds) ok"));
}

TEST(BluetoothService, TheRealServiceIsUnavailableOffscreen) {
  Bluetooth service;
  EXPECT_FALSE(service.available());
}

TEST(BluetoothServiceStartup, UnknownThenKnownShowsDevicesWithoutAnEvent) {
  Fixture f;
  f.service->setRetrySchedule({10, 10, 10, 10});
  f.fake->publish(unknownStatus());
  EXPECT_FALSE(f.service->radioOn());
  wait(60);
  EXPECT_EQ(f.fake->refreshes, 1);  // the retry asked the backend again
  f.fake->publish(knownStatus());
  EXPECT_TRUE(f.service->radioOn());
  EXPECT_EQ(f.service->connectedCount(), 1);
  wait(60);
  EXPECT_EQ(f.fake->refreshes, 1);  // a known state stops the retries
}

TEST(BluetoothServiceStartup, UnknownKeepsTheLastKnownState) {
  Fixture f;
  f.service->setRetrySchedule({10});
  f.fake->publish(knownStatus());
  f.fake->publish(unknownStatus());
  EXPECT_TRUE(f.service->radioOn());
  EXPECT_EQ(f.service->connectedCount(), 1);
}

TEST(BluetoothServiceStartup, RetriesStopAfterTheScheduleAndTheRadioShowsOff) {
  Fixture f;
  f.service->setRetrySchedule({5, 5});
  f.fake->publish(knownStatus());
  for (int i = 0; i < 3; ++i) {
    f.fake->publish(unknownStatus());
    wait(40);
  }
  EXPECT_EQ(f.fake->refreshes, 2);
  EXPECT_FALSE(f.service->radioOn());
  EXPECT_TRUE(f.service->devices().isEmpty());
  wait(60);
  EXPECT_EQ(f.fake->refreshes, 2);
}

TEST(BluetoothServiceStartup, AGenuinelyOffRadioStaysOffWithoutRetries) {
  Fixture f;
  f.service->setRetrySchedule({5, 5});
  f.fake->publish(BluetoothStatus{});  // radio off, known
  wait(40);
  EXPECT_EQ(f.fake->refreshes, 0);
  EXPECT_FALSE(f.service->radioOn());
}

TEST(BluetoothServiceStartup, TheSafetyNetPollsOnlyWhileObserved) {
  Fixture f;
  f.service->setSafetyInterval(10);
  EXPECT_FALSE(f.service->safetyTimerActive());
  wait(40);
  EXPECT_EQ(f.fake->refreshes, 0);
  {
    QSignalSpy observer(f.service.get(), &Bluetooth::changed);
    EXPECT_TRUE(f.service->safetyTimerActive());
    wait(60);
    EXPECT_GT(f.fake->refreshes, 0);
  }
  wait(20);  // the queued disconnect check runs
  EXPECT_FALSE(f.service->safetyTimerActive());
  const int after = f.fake->refreshes;
  wait(60);
  EXPECT_EQ(f.fake->refreshes, after);
}
