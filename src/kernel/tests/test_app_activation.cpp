#include <gtest/gtest.h>

#include "kernel/win32/app_activation.h"

// Only the paths that touch no real window: empty and unknown inputs fail soft.
TEST(KernelAppActivation, EmptyAumidActivatesNothing) {
  EXPECT_FALSE(wabar::kernel::activateAppByAumid(QString()));
}

TEST(KernelAppActivation, AnExeAumidIsItsOwnProcessName) {
  EXPECT_EQ(wabar::kernel::exeNameForAumid(QStringLiteral("C:\\Apps\\player.exe")),
            QStringLiteral("player.exe"));
  EXPECT_TRUE(wabar::kernel::exeNameForAumid(QString()).isEmpty());
}

TEST(KernelAppActivation, UnknownExeHasNoAppName) {
  EXPECT_TRUE(wabar::kernel::appNameForExe(QString()).isEmpty());
  EXPECT_TRUE(wabar::kernel::appNameForWindowAumid(QString()).isEmpty());
}
