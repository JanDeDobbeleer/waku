#include <gtest/gtest.h>

#include <QDateTime>
#include <QString>
#include <QTimeZone>
#include <optional>

#include "features/notifications/toast_activation.h"
#include "features/notifications/toast_xml.h"

using namespace wabar::notifications;

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

TEST(ToastXml, ReadsTheScenario) {
  const auto urgent = parseToastXml(
      "<toast scenario=\"urgent\"><visual><binding template=\"ToastGeneric\"/></visual></toast>");
  ASSERT_TRUE(urgent.has_value());
  EXPECT_EQ(urgent->scenario, "urgent");
  const auto plain =
      parseToastXml("<toast><visual><binding template=\"ToastGeneric\"/></visual></toast>");
  ASSERT_TRUE(plain.has_value());
  EXPECT_TRUE(plain->scenario.isEmpty());
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
