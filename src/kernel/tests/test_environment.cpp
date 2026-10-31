#include <gtest/gtest.h>

#include <QtGlobal>

#include "kernel/win32/environment.h"

using wabar::kernel::expandEnvironment;

TEST(KernelEnvironment, ExpandsKnownVariables) {
  qputenv("WABAR_TEST_EXPAND", "C:\\Some Folder");
  EXPECT_EQ(expandEnvironment(QStringLiteral("%WABAR_TEST_EXPAND%\\app.exe")),
            QStringLiteral("C:\\Some Folder\\app.exe"));
  qunsetenv("WABAR_TEST_EXPAND");
}

TEST(KernelEnvironment, KeepsUnknownVariablesAndPlainText) {
  EXPECT_EQ(expandEnvironment(QStringLiteral("%WABAR_TEST_NOT_SET_X%\\a")),
            QStringLiteral("%WABAR_TEST_NOT_SET_X%\\a"));
  EXPECT_EQ(expandEnvironment(QStringLiteral("C:\\plain")), QStringLiteral("C:\\plain"));
  EXPECT_EQ(expandEnvironment(QString()), QString());
}
