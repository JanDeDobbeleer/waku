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
