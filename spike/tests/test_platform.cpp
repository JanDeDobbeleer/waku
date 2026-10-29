// Platform tests carried over from the retired Widgets build: network connection classification.
#include <gtest/gtest.h>

#include <QDateTime>
#include <QStringList>
#include <QTimeZone>
#include <optional>
#include <utility>
#include <vector>

#include "platform/command_line.h"
#include "platform/connectivity.h"

using namespace yasb::platform;

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

TEST(CommandLine, QuoteCmdLineFollowsList2CmdLine) {
  EXPECT_EQ(quoteCmdLine({"notepad", "a b", ""}), "notepad \"a b\" \"\"");
  EXPECT_EQ(quoteCmdLine({"say \"hi\""}), "\"say \\\"hi\\\"\"");
  EXPECT_EQ(quoteCmdLine({"C:\\dir with space\\"}), "\"C:\\dir with space\\\\\"");
}
