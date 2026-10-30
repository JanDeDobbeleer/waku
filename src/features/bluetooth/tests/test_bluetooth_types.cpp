#include <gtest/gtest.h>

#include <QStringList>
#include <utility>

#include "features/bluetooth/bluetooth.h"

using namespace wabar::bluetooth;

TEST(BluetoothTypes, ClassOfDeviceMapping) {
  EXPECT_EQ(formatBtAddress(0xAABBCC001122ULL), "AA:BB:CC:00:11:22");
  EXPECT_EQ(parseClassOfDevice(0x240404), (std::pair<int, int>{4, 1}));
  EXPECT_EQ(deviceTypeFromCod(4, 1), BtDeviceType::Headset);
  EXPECT_EQ(deviceTypeFromCod(4, 6), BtDeviceType::Headphones);
  EXPECT_EQ(deviceTypeFromCod(4, 5), BtDeviceType::Speaker);
  EXPECT_EQ(deviceTypeFromCod(2, 0), BtDeviceType::Phone);
  EXPECT_EQ(deviceTypeFromCod(5, 0x10), BtDeviceType::Keyboard);
  EXPECT_EQ(deviceTypeFromCod(5, 0x20), BtDeviceType::Mouse);
  EXPECT_EQ(deviceTypeFromCod(5, 0x02), BtDeviceType::Controller);
  EXPECT_EQ(deviceTypeFromCod(7, 0), BtDeviceType::Watch);
  EXPECT_EQ(deviceTypeFromCod(6, 0x08), BtDeviceType::Camera);
  EXPECT_EQ(deviceTypeFromCod(99, 0), BtDeviceType::Generic);
  EXPECT_EQ(profilesFromCod(4, true), (QStringList{"mic", "audio"}));
  EXPECT_TRUE(profilesFromCod(4, false).isEmpty());
  EXPECT_EQ(profilesFromCod(5, true), QStringList{"input"});
}

TEST(BluetoothTypes, PreferKnownThenConnected) {
  BluetoothDevice known;
  known.paired = true;
  BluetoothDevice unknown;
  unknown.connected = true;
  EXPECT_TRUE(preferDevice(known, unknown));
  EXPECT_FALSE(preferDevice(unknown, known));
  BluetoothDevice a, b;
  b.connected = true;
  EXPECT_TRUE(preferDevice(b, a));
  EXPECT_FALSE(preferDevice(a, b));
}

namespace {

BluetoothDevice entry(const QString& name, const QString& address, bool le) {
  BluetoothDevice device;
  device.name = name;
  device.address = address;
  device.isLe = le;
  device.paired = true;
  return device;
}

}  // namespace

TEST(BluetoothMerge, SameContainerCollapsesClassicAndLe) {
  BluetoothDevice le = entry("Jan 1337", "AA:00:00:00:00:02", true);
  le.containerId = "{C1}";
  BluetoothDevice classic = entry("Jan 1337", "AA:00:00:00:00:01", false);
  classic.containerId = "{c1}";
  classic.deviceType = BtDeviceType::Headphones;
  const auto merged = mergeSameDevices({le, classic});
  ASSERT_EQ(merged.size(), 1u);
  EXPECT_EQ(merged[0].address, "AA:00:00:00:00:01");
  EXPECT_FALSE(merged[0].isLe);
  EXPECT_EQ(merged[0].otherAddresses, QStringList{"AA:00:00:00:00:02"});
  EXPECT_EQ(merged[0].deviceType, BtDeviceType::Headphones);
}

TEST(BluetoothMerge, SameNameDifferentContainersStayApart) {
  BluetoothDevice a = entry("Jan 1337", "AA:00:00:00:00:01", false);
  a.containerId = "{C1}";
  BluetoothDevice b = entry("Jan 1337", "AA:00:00:00:00:02", true);
  b.containerId = "{C2}";
  EXPECT_EQ(mergeSameDevices({a, b}).size(), 2u);
}

TEST(BluetoothMerge, FallsBackToNameAndTypeWithoutContainer) {
  BluetoothDevice classic = entry("Buds", "AA:00:00:00:00:01", false);
  classic.deviceType = BtDeviceType::Headphones;
  BluetoothDevice le = entry("buds", "AA:00:00:00:00:02", true);
  le.deviceType = BtDeviceType::Headphones;
  EXPECT_EQ(mergeSameDevices({classic, le}).size(), 1u);
  // Another type, another device.
  le.deviceType = BtDeviceType::Phone;
  EXPECT_EQ(mergeSameDevices({classic, le}).size(), 2u);
  // Two classic entries with one name are never merged by name.
  BluetoothDevice second = classic;
  second.address = "AA:00:00:00:00:03";
  EXPECT_EQ(mergeSameDevices({classic, second}).size(), 2u);
}

TEST(BluetoothMerge, ConnectedAndBatteryCombine) {
  BluetoothDevice classic = entry("Jan 1337", "AA:00:00:00:00:01", false);
  classic.containerId = "{C1}";
  classic.paired = false;
  classic.remembered = true;
  BluetoothDevice le = entry("Jan 1337", "AA:00:00:00:00:02", true);
  le.containerId = "{C1}";
  le.connected = true;
  le.battery = 80;
  const auto merged = mergeSameDevices({classic, le});
  ASSERT_EQ(merged.size(), 1u);
  EXPECT_TRUE(merged[0].connected);
  EXPECT_TRUE(merged[0].paired);
  EXPECT_TRUE(merged[0].remembered);
  EXPECT_EQ(merged[0].battery, std::optional<int>(80));
}
