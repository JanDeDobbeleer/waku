#include <gtest/gtest.h>

#include "kernel/win32/app_activation.h"

// Only the paths that touch no real window: empty and unknown inputs fail soft.
TEST(KernelAppActivation, EmptyAumidActivatesNothing) {
  EXPECT_FALSE(waku::kernel::activateAppByAumid(QString()));
}

TEST(KernelAppActivation, AnExeAumidIsItsOwnProcessName) {
  EXPECT_EQ(waku::kernel::exeNameForAumid(QStringLiteral("C:\\Apps\\player.exe")),
            QStringLiteral("player.exe"));
  EXPECT_TRUE(waku::kernel::exeNameForAumid(QString()).isEmpty());
}

TEST(KernelAppActivation, UnknownExeHasNoAppName) {
  EXPECT_TRUE(waku::kernel::appNameForExe(QString()).isEmpty());
  EXPECT_TRUE(waku::kernel::appNameForWindowAumid(QString()).isEmpty());
}
