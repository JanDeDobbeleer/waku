#include <gtest/gtest.h>

#include <QGuiApplication>

#include "kernel/qt/live_desktop.h"

using namespace wabar::kernel;

TEST(KernelLiveDesktop, TheTestPlatformIsNotALiveDesktop) {
  ASSERT_EQ(QGuiApplication::platformName(), QStringLiteral("offscreen"));
  EXPECT_FALSE(liveDesktop());
  EXPECT_FALSE(liveDesktop(QStringLiteral("anything")));
}

TEST(KernelLiveDesktop, CapabilitiesAreSwitchedOffByName) {
  const QString tray = QStringLiteral("kernel-test-tray");
  const QString other = QStringLiteral("kernel-test-other");
  EXPECT_FALSE(capabilityDisabled(tray));
  setCapabilityDisabled(tray, true);
  EXPECT_TRUE(capabilityDisabled(tray));
  EXPECT_FALSE(capabilityDisabled(other));
  setCapabilityDisabled(tray, false);
  EXPECT_FALSE(capabilityDisabled(tray));
}

TEST(KernelLiveDesktop, TheDisableFlagIsIndependentOfThePlatform) {
  const QString name = QStringLiteral("kernel-test-flag");
  setCapabilityDisabled(name, true);
  EXPECT_TRUE(capabilityDisabled(name));
  EXPECT_FALSE(liveDesktop(name));
  setCapabilityDisabled(name, false);
}
