#include <gtest/gtest.h>

#include <optional>

#include "features/network/connectivity.h"

using namespace waku::network;

namespace {

ConnectionSnapshot wlan() { return {"Home", true, 71, 3}; }
ConnectionSnapshot ethernet() { return {"Network", false, 6, std::nullopt}; }
ConnectionSnapshot wireguard() { return {"ProtonVPN", false, 53, std::nullopt}; }

}  // namespace

TEST(Connectivity, VpnOverWifiKeepsTheWifiNetwork) {
  const WifiInfo info = classifyConnections({wireguard(), wlan()}, wireguard());
  EXPECT_FALSE(info.ethernet);
  EXPECT_EQ(info.name, "Home");
  EXPECT_EQ(info.bars, 3);
  EXPECT_EQ(info.vpnName, "ProtonVPN");
}

TEST(Connectivity, VpnOverEthernetShowsEthernet) {
  const WifiInfo info = classifyConnections({wireguard(), ethernet()}, wireguard());
  EXPECT_TRUE(info.ethernet);
  EXPECT_EQ(info.vpnName, "ProtonVPN");
}

TEST(Connectivity, WithoutVpn) {
  WifiInfo info = classifyConnections({wlan()}, wlan());
  EXPECT_FALSE(info.ethernet);
  EXPECT_EQ(info.name, "Home");
  EXPECT_EQ(info.vpnName, "");
  info = classifyConnections({ethernet(), wlan()}, ethernet());
  EXPECT_TRUE(info.ethernet);
  EXPECT_EQ(info.name, "Home");
  info = classifyConnections({}, std::nullopt);
  EXPECT_FALSE(info.ethernet);
  EXPECT_EQ(info.name, "Disconnected");
  EXPECT_EQ(info.bars, 0);
}

TEST(Connectivity, VpnInterfaceTypes) {
  for (int type : {23, 53, 131}) EXPECT_TRUE(isVpnInterfaceType(type));
  for (int type : {6, 71, 0}) EXPECT_FALSE(isVpnInterfaceType(type));
}

TEST(PickLocalAddress, NothingWithoutAdapters) {
  EXPECT_FALSE(pickLocalAddress({}).has_value());
  EXPECT_FALSE(pickLocalAddress({{"", true, 1}}).has_value());
}

TEST(PickLocalAddress, AGatewayAdapterBeatsOneWithoutEvenWithAWorseMetric) {
  const auto address = pickLocalAddress({{"192.168.56.1", false, 5}, {"10.0.0.7", true, 50}});
  EXPECT_EQ(address, "10.0.0.7");
}

TEST(PickLocalAddress, TheLowestMetricWinsAmongGatewayAdapters) {
  const auto address = pickLocalAddress({{"10.0.0.7", true, 50}, {"192.168.1.9", true, 25}});
  EXPECT_EQ(address, "192.168.1.9");
}

TEST(PickLocalAddress, TheFirstOfEqualAdaptersWins) {
  const auto address = pickLocalAddress({{"10.0.0.7", true, 25}, {"192.168.1.9", true, 25}});
  EXPECT_EQ(address, "10.0.0.7");
}

TEST(PickLocalAddress, LinkLocalOnlyWinsWhenNothingElseExists) {
  EXPECT_EQ(pickLocalAddress({{"169.254.3.4", true, 1}, {"172.16.0.2", false, 90}}), "172.16.0.2");
  EXPECT_EQ(pickLocalAddress({{"169.254.3.4", false, 1}}), "169.254.3.4");
}
