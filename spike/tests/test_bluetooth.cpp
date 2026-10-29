#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QVariantMap>
#include <memory>

#include "spike/services/bluetooth_service.h"

using namespace tidy::services;
namespace platform = yasb::platform;

namespace {

class FakeBackend final : public platform::BluetoothBackend {
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
  void publish(const platform::BluetoothStatus& status) { publishStatus(status); }

  int starts = 0;
  int refreshes = 0;
  std::vector<bool> radioRequests;
  QStringList connects;
  QStringList disconnects;
};

platform::BluetoothDevice device(const QString& name, bool connected, bool paired = true,
                                 std::optional<int> battery = std::nullopt,
                                 platform::BtDeviceType type = platform::BtDeviceType::Headphones) {
  platform::BluetoothDevice d;
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
  platform::BluetoothStatus status;
  status.radioOn = true;
  status.devices = {
      device(QStringLiteral("Mouse"), false, true, std::nullopt, platform::BtDeviceType::Mouse),
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
  platform::BluetoothStatus status;
  status.radioOn = false;
  status.devices = {device(QStringLiteral("Buds"), true)};
  f.fake->publish(status);
  EXPECT_EQ(f.service->connectedCount(), 0);
  EXPECT_TRUE(f.service->connectedNames().isEmpty());
}

TEST(BluetoothService, UnpairedDevicesAreNotListedOrCounted) {
  Fixture f;
  platform::BluetoothStatus status;
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
  emit f.fake->connectionFinished(true, QString(), platform::BluetoothDevice());
  EXPECT_EQ(f.fake->refreshes, 1);
}

TEST(BluetoothService, TheRealServiceIsUnavailableOffscreen) {
  Bluetooth service;
  EXPECT_FALSE(service.available());
}
