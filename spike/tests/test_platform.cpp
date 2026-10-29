// Platform tests carried over from the retired Widgets build: toast XML, toast activation, network
// connection classification.
#include <gtest/gtest.h>

#include <QDateTime>
#include <QStringList>
#include <QTimeZone>
#include <optional>
#include <utility>
#include <vector>

#include "platform/command_line.h"
#include "platform/connectivity.h"
#include "platform/toast_activation.h"
#include "platform/toast_xml.h"

using namespace yasb::platform;

namespace {

const char* kToastXml =
    "<toast launch=\"action=open&amp;id=7\" activationType=\"protocol\" "
    "displayTimestamp=\"2026-03-05T09:30:00+02:00\">"
    "<visual><binding template=\"ToastGeneric\">"
    "<text>Title</text><text> Body line </text><text>  </text>"
    "<text placement=\"attribution\">via Mail</text>"
    "<image placement=\"appLogoOverride\" hint-crop=\"circle\" src=\"file:///C:/logo.png\"/>"
    "<image placement=\"hero\" src=\"C:\\hero.png\"/>"
    "<image src=\"https://example.com/a.png\"/>"
    "</binding></visual></toast>";

}  // namespace

TEST(ToastXml, ParsesTextsAttributionLaunchAndImages) {
  const auto toast = parseToastXml(QString::fromLatin1(kToastXml));
  ASSERT_TRUE(toast.has_value());
  EXPECT_EQ(toast->texts, (QStringList{"Title", "Body line"}));
  EXPECT_EQ(toast->attribution, "via Mail");
  EXPECT_EQ(toast->launch, "action=open&id=7");
  EXPECT_EQ(toast->activationType, "protocol");
  ASSERT_TRUE(toast->displayTimestamp.has_value());
  EXPECT_EQ(toast->displayTimestamp->toUTC(),
            QDateTime(QDate(2026, 3, 5), QTime(7, 30), QTimeZone::UTC));
  ASSERT_NE(toast->findImage("appLogoOverride"), nullptr);
  EXPECT_EQ(toast->findImage("appLogoOverride")->hintCrop, "circle");
  ASSERT_NE(toast->findImage("hero"), nullptr);
  ASSERT_NE(toast->findInlineImage(), nullptr);
  EXPECT_EQ(toast->findInlineImage()->src, "https://example.com/a.png");
  EXPECT_EQ(toast->findImage("missing"), nullptr);
}

TEST(ToastXml, DefaultsAndBrokenXml) {
  const auto bare =
      parseToastXml("<toast><visual><binding template=\"ToastGeneric\"/></visual></toast>");
  ASSERT_TRUE(bare.has_value());
  EXPECT_TRUE(bare->texts.isEmpty());
  EXPECT_EQ(bare->activationType, "foreground");
  EXPECT_FALSE(bare->displayTimestamp.has_value());
  EXPECT_FALSE(parseToastXml("<toast><visual>").has_value());
  EXPECT_FALSE(parseToastXml("").has_value());
}

TEST(ToastXml, LegacyImageTemplatesShowTheirImageAsTheLogo) {
  const auto toast = parseToastXml(
      "<toast><visual><binding template=\"ToastImageAndText01\"><image id=\"1\" src=\"C:\\a.png\"/>"
      "<text id=\"1\">Hi</text></binding></visual></toast>");
  ASSERT_TRUE(toast.has_value());
  ASSERT_NE(toast->findImage("appLogoOverride"), nullptr);
  EXPECT_EQ(toast->findImage("appLogoOverride")->src, "C:\\a.png");
  EXPECT_EQ(toast->findInlineImage(), nullptr);
  EXPECT_EQ(toast->findImage("hero"), nullptr);
}

TEST(ToastXml, PopToastMatchesByTextAndRemovesIt) {
  std::vector<ToastXml> toasts(3);
  toasts[0].texts = {"a"};
  toasts[1].texts = {"b", "c"};
  toasts[1].launch = "second";
  toasts[2].texts = {"b", "c"};
  toasts[2].launch = "third";
  EXPECT_EQ(popToast(toasts, {"b", "c"})->launch, "second");
  EXPECT_EQ(popToast(toasts, {"b", "c"})->launch, "third");
  EXPECT_FALSE(popToast(toasts, {"b", "c"}).has_value());
  EXPECT_EQ(toasts.size(), 1u);
}

TEST(ToastActivation, OnlyRealLinksOpenAsProtocolActivation) {
  EXPECT_TRUE(isOpenableToastUri("https://example.com/x"));
  EXPECT_TRUE(isOpenableToastUri("ms-settings:network"));
  EXPECT_TRUE(isOpenableToastUri("myapp://open?id=1"));
  EXPECT_FALSE(isOpenableToastUri("file:///C:/Windows/notepad.exe"));
  EXPECT_FALSE(isOpenableToastUri("FILE:///C:/x"));
  EXPECT_FALSE(isOpenableToastUri("C:\\Windows\\notepad.exe"));  // a drive letter is no scheme
  EXPECT_FALSE(isOpenableToastUri("action=open"));
  EXPECT_FALSE(isOpenableToastUri(""));
  EXPECT_FALSE(activateToast("app", "x", "system"));
}

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
