#include <gtest/gtest.h>

#include "features/keymap/window_match.h"

using namespace waku::keymap;

TEST(KeymapWindowMatch, ProcessNamesIgnoreCaseAndExtension) {
  EXPECT_TRUE(processNamesMatch(QStringLiteral("Firefox"), QStringLiteral("firefox.exe")));
  EXPECT_TRUE(processNamesMatch(QStringLiteral("firefox.exe"), QStringLiteral("FIREFOX")));
  EXPECT_TRUE(processNamesMatch(QStringLiteral("wt.exe"), QStringLiteral("wt.exe")));
  EXPECT_FALSE(processNamesMatch(QStringLiteral("fire"), QStringLiteral("firefox.exe")));
  EXPECT_FALSE(processNamesMatch(QString(), QString()));
  EXPECT_FALSE(processNamesMatch(QStringLiteral(".exe"), QStringLiteral("a.exe")));
}

TEST(KeymapWindowMatch, TargetIsAnExecutableOrATitlePart) {
  EXPECT_TRUE(windowMatchesTarget(QStringLiteral("code"), QStringLiteral("Code.exe"),
                                  QStringLiteral("main.cpp - waku")));
  EXPECT_TRUE(windowMatchesTarget(QStringLiteral("waku"), QStringLiteral("Code.exe"),
                                  QStringLiteral("main.cpp - WAKU - Visual Studio Code")));
  EXPECT_FALSE(windowMatchesTarget(QStringLiteral("firefox"), QStringLiteral("Code.exe"),
                                   QStringLiteral("main.cpp")));
  EXPECT_FALSE(windowMatchesTarget(QString(), QStringLiteral("Code.exe"), QStringLiteral("x")));
}
