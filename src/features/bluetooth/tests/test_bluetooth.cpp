#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>
#include <QVariantMap>
#include <memory>

#include "features/bluetooth/qml/bluetooth_service.h"
#include "features/bluetooth/testing/fake_bluetooth_backend.h"

using namespace wabar::bluetooth;

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
