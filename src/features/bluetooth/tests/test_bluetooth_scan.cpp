// Scan, nearby devices and pairing of the Bluetooth service, on the fake backend: nothing here
// scans, pairs or forgets with real hardware.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>
#include <QVariantMap>
#include <memory>

#include "features/bluetooth/qml/bluetooth_service.h"
#include "features/bluetooth/testing/fake_bluetooth_backend.h"

using namespace waku::bluetooth;

namespace {

BluetoothDevice makeDevice(const QString& name, bool paired, const QString& container = QString()) {
  BluetoothDevice d;
  d.name = name;
  d.address = QStringLiteral("addr-") + name;
  d.paired = paired;
  d.containerId = container;
  d.deviceType = BtDeviceType::Headphones;
  return d;
}

struct Fixture {
  Fixture() {
    auto owned = std::make_unique<FakeBackend>();
    fake = owned.get();
    service = std::make_unique<Bluetooth>(std::move(owned), nullptr);
    BluetoothStatus status;
    status.radioOn = true;
    status.devices = {makeDevice(QStringLiteral("Buds"), true, QStringLiteral("c-buds"))};
    fake->publish(status);
  }
  QStringList names() const {
    QStringList result;
    for (const QVariant& v : service->devices())
      result << v.toMap().value(QStringLiteral("name")).toString();
    return result;
  }
  FakeBackend* fake = nullptr;
  std::unique_ptr<Bluetooth> service;
};

void wait(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}

}  // namespace

TEST(BluetoothScan, ScanningTogglesAroundTheBackendScan) {
  Fixture f;
  QSignalSpy scanning(f.service.get(), &Bluetooth::scanningChanged);
  EXPECT_FALSE(f.service->scanning());
  f.service->scan();
  EXPECT_TRUE(f.service->scanning());
  EXPECT_EQ(f.fake->scans, 1);
  f.service->scan();  // already running
  EXPECT_EQ(f.fake->scans, 1);
  f.fake->finishScan(BtScanStatus::Success, {});
  EXPECT_FALSE(f.service->scanning());
  EXPECT_EQ(scanning.count(), 2);
  EXPECT_TRUE(f.service->error().isEmpty());
}

TEST(BluetoothScan, NearbyDevicesJoinTheListAsUnpaired) {
  Fixture f;
  QSignalSpy changed(f.service.get(), &Bluetooth::changed);
  f.service->scan();
  f.fake->finishScan(BtScanStatus::Success, {makeDevice(QStringLiteral("Zed"), false),
                                             makeDevice(QStringLiteral("Amp"), false)});
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(f.names(),
            (QStringList{QStringLiteral("Buds"), QStringLiteral("Amp"), QStringLiteral("Zed")}));
  const QVariantMap amp = f.service->devices()[1].toMap();
  EXPECT_FALSE(amp.value(QStringLiteral("paired")).toBool());
  EXPECT_FALSE(amp.value(QStringLiteral("connected")).toBool());
  EXPECT_EQ(amp.value(QStringLiteral("id")).toString(), QStringLiteral("addr-Amp"));
  // Nearby devices are not counted as connected.
  EXPECT_EQ(f.service->connectedCount(), 0);
}

TEST(BluetoothScan, ANearbyEntryOfAListedDeviceIsNotRepeated) {
  Fixture f;
  BluetoothDevice sameContainer =
      makeDevice(QStringLiteral("Buds LE"), false, QStringLiteral("c-buds"));
  BluetoothDevice sameAddress = makeDevice(QStringLiteral("Buds"), false);
  f.service->scan();
  f.fake->finishScan(BtScanStatus::Success,
                     {sameContainer, sameAddress, makeDevice(QStringLiteral("Other"), false)});
  EXPECT_EQ(f.names(), (QStringList{QStringLiteral("Buds"), QStringLiteral("Other")}));
}

TEST(BluetoothScan, ANewScanReplacesTheNearbyDevices) {
  Fixture f;
  f.service->scan();
  f.fake->finishScan(BtScanStatus::Success, {makeDevice(QStringLiteral("One"), false)});
  f.service->scan();
  f.fake->finishScan(BtScanStatus::Success, {makeDevice(QStringLiteral("Two"), false)});
  EXPECT_EQ(f.names(), (QStringList{QStringLiteral("Buds"), QStringLiteral("Two")}));
}

TEST(BluetoothScan, RadioOffClearsTheNearbyDevices) {
  Fixture f;
  f.service->scan();
  f.fake->finishScan(BtScanStatus::Success, {makeDevice(QStringLiteral("One"), false)});
  f.fake->publish(BluetoothStatus{});  // radio off
  EXPECT_TRUE(f.names().isEmpty());
}

TEST(BluetoothScan, FailuresSetTheError) {
  Fixture f;
  QSignalSpy errors(f.service.get(), &Bluetooth::errorChanged);
  f.service->scan();
  f.fake->finishScan(BtScanStatus::RadioOff, {});
  EXPECT_FALSE(f.service->scanning());
  EXPECT_EQ(f.service->error(), QStringLiteral("Bluetooth is off"));
  f.service->scan();  // a new scan clears the error
  EXPECT_TRUE(f.service->error().isEmpty());
  f.fake->finishScan(BtScanStatus::Error, {});
  EXPECT_EQ(f.service->error(), QStringLiteral("Bluetooth scan failed"));
  EXPECT_EQ(errors.count(), 3);
}

TEST(BluetoothScan, ScanStartedFromTheBackendAlsoMarksScanning) {
  Fixture f;
  emit f.fake->scanStarted();
  EXPECT_TRUE(f.service->scanning());
  f.fake->finishScan(BtScanStatus::Success, {});
  EXPECT_FALSE(f.service->scanning());
}

TEST(BluetoothScan, NothingScansUnlessAskedAndAnUnobservedServiceStaysQuiet) {
  Fixture f;
  f.service->setSafetyInterval(10);
  wait(60);
  EXPECT_EQ(f.fake->scans, 0);
  EXPECT_EQ(f.fake->refreshes, 0);  // no listener: the safety net does not run
  {
    QSignalSpy observer(f.service.get(), &Bluetooth::changed);
    wait(40);
    EXPECT_EQ(f.fake->scans, 0);  // the safety net refreshes, it never scans
  }
}

TEST(BluetoothPairing, PairSuccessRefreshesAndDropsTheNearbyEntry) {
  Fixture f;
  f.service->scan();
  f.fake->finishScan(BtScanStatus::Success, {makeDevice(QStringLiteral("New"), false)});
  QSignalSpy pairing(f.service.get(), &Bluetooth::pairingChanged);
  f.service->pairDevice(QStringLiteral("addr-New"));
  EXPECT_EQ(f.fake->pairs, QStringList{QStringLiteral("addr-New")});
  EXPECT_TRUE(f.service->pairing());
  f.service->pairDevice(QStringLiteral("addr-Other"));  // one at a time
  EXPECT_EQ(f.fake->pairs.size(), 1);
  const int refreshes = f.fake->refreshes;
  f.fake->finishPairing(true, QStringLiteral("Device paired"),
                        makeDevice(QStringLiteral("New"), true));
  EXPECT_FALSE(f.service->pairing());
  EXPECT_EQ(pairing.count(), 2);
  EXPECT_TRUE(f.service->error().isEmpty());
  EXPECT_EQ(f.fake->refreshes, refreshes + 1);
  EXPECT_EQ(f.names(), QStringList{QStringLiteral("Buds")});  // the refresh lists it when paired
}

TEST(BluetoothPairing, PairFailureSetsTheError) {
  Fixture f;
  f.service->pairDevice(QStringLiteral("addr-New"));
  f.fake->finishPairing(false, QStringLiteral("Pairing failed"),
                        makeDevice(QStringLiteral("New"), false));
  EXPECT_FALSE(f.service->pairing());
  EXPECT_EQ(f.service->error(), QStringLiteral("Pairing failed"));
  f.service->pairDevice(QStringLiteral("addr-New"));  // a new attempt clears the error
  EXPECT_TRUE(f.service->error().isEmpty());
}

TEST(BluetoothPairing, ADeviceThatNeedsAPinReportsThePairInSettingsError) {
  Fixture f;
  f.service->pairDevice(QStringLiteral("addr-Pin"));
  f.fake->finishPairing(false, pairNeedsSettingsMessage(),
                        makeDevice(QStringLiteral("Pin"), false));
  EXPECT_EQ(f.service->error(), QStringLiteral("This device needs pairing in Windows Settings"));
  EXPECT_FALSE(f.service->pairing());
}

TEST(BluetoothPairing, ABusyBackendRefusesAndSetsAnError) {
  Fixture f;
  f.fake->busy = true;
  f.service->pairDevice(QStringLiteral("addr-New"));
  EXPECT_FALSE(f.service->pairing());
  EXPECT_EQ(f.service->error(), QStringLiteral("Another Bluetooth operation is in progress"));
}

TEST(BluetoothPairing, ForgetGoesToTheBackendAndRefreshes) {
  Fixture f;
  f.service->forgetDevice(QStringLiteral("addr-Buds"));
  EXPECT_EQ(f.fake->unpairs, QStringList{QStringLiteral("addr-Buds")});
  EXPECT_TRUE(f.service->pairing());
  const int refreshes = f.fake->refreshes;
  f.fake->finishPairing(true, QStringLiteral("Device forgotten"),
                        makeDevice(QStringLiteral("Buds"), false));
  EXPECT_FALSE(f.service->pairing());
  EXPECT_EQ(f.fake->refreshes, refreshes + 1);
}

TEST(BluetoothPairing, CancelReachesTheBackendOnlyWhilePairing) {
  Fixture f;
  f.service->cancelPair();
  EXPECT_EQ(f.fake->cancels, 0);
  f.service->pairDevice(QStringLiteral("addr-New"));
  f.service->cancelPair();
  EXPECT_EQ(f.fake->cancels, 1);
  f.fake->finishPairing(false, QStringLiteral("Pairing cancelled"),
                        makeDevice(QStringLiteral("New"), false));
  EXPECT_FALSE(f.service->pairing());
  EXPECT_EQ(f.service->error(), QStringLiteral("Pairing cancelled"));
}

TEST(BluetoothPairing, WithoutABackendEverythingIsInert) {
  Bluetooth service(nullptr, nullptr);
  service.scan();
  service.pairDevice(QStringLiteral("x"));
  service.forgetDevice(QStringLiteral("x"));
  service.cancelPair();
  EXPECT_FALSE(service.scanning());
  EXPECT_FALSE(service.pairing());
  EXPECT_TRUE(service.error().isEmpty());
}
