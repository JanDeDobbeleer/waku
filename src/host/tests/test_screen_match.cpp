#include <gtest/gtest.h>

#include "host/screen_match.h"

using namespace waku::host;

namespace {

std::vector<ScreenRef> threeScreens() {
  return {{QStringLiteral("\\\\.\\DISPLAY1"), true},
          {QStringLiteral("\\\\.\\DISPLAY2"), false},
          {QStringLiteral("\\\\.\\DISPLAY3"), false}};
}

using Indices = std::vector<int>;

}  // namespace

TEST(ScreenMatch, PrimarySelectsOnlyThePrimaryScreen) {
  EXPECT_EQ(matchScreens({QStringLiteral("primary")}, threeScreens()), (Indices{0}));
}

TEST(ScreenMatch, AllSelectsEveryScreenInOrder) {
  EXPECT_EQ(matchScreens({QStringLiteral("all")}, threeScreens()), (Indices{0, 1, 2}));
}

TEST(ScreenMatch, ANameMatchesWithOrWithoutTheDevicePrefix) {
  EXPECT_EQ(matchScreens({QStringLiteral("\\\\.\\DISPLAY2")}, threeScreens()), (Indices{1}));
  EXPECT_EQ(matchScreens({QStringLiteral("DISPLAY3")}, threeScreens()), (Indices{2}));
}

TEST(ScreenMatch, MatchingIgnoresCase) {
  EXPECT_EQ(matchScreens({QStringLiteral("display2"), QStringLiteral("PRIMARY")}, threeScreens()),
            (Indices{0, 1}));
  EXPECT_EQ(matchScreens({QStringLiteral("All")}, threeScreens()), (Indices{0, 1, 2}));
}

TEST(ScreenMatch, ANamePartIsNotAMatch) {
  // "DISPLAY" or "PLAY2" must not select DISPLAY2: only the whole name after the prefix counts.
  EXPECT_TRUE(matchScreens({QStringLiteral("DISPLAY")}, threeScreens()).empty());
  EXPECT_TRUE(matchScreens({QStringLiteral("PLAY2")}, threeScreens()).empty());
}

TEST(ScreenMatch, EachScreenIsSelectedOnceInScreenOrder) {
  EXPECT_EQ(matchScreens({QStringLiteral("DISPLAY3"), QStringLiteral("all"),
                          QStringLiteral("primary"), QStringLiteral("DISPLAY1")},
                         threeScreens()),
            (Indices{0, 1, 2}));
  EXPECT_EQ(matchScreens({QStringLiteral("DISPLAY3"), QStringLiteral("DISPLAY1")}, threeScreens()),
            (Indices{0, 2}));
}

TEST(ScreenMatch, UnknownPatternsAndEmptyListsSelectNothing) {
  EXPECT_TRUE(matchScreens({QStringLiteral("DISPLAY9")}, threeScreens()).empty());
  EXPECT_TRUE(matchScreens({}, threeScreens()).empty());
  EXPECT_TRUE(matchScreens({QStringLiteral(" "), QString()}, threeScreens()).empty());
}

TEST(ScreenMatch, NoPrimaryScreenMeansPrimaryMatchesNothing) {
  const std::vector<ScreenRef> none = {{QStringLiteral("\\\\.\\DISPLAY1"), false}};
  EXPECT_TRUE(matchScreens({QStringLiteral("primary")}, none).empty());
}

TEST(ScreenMatch, NoScreensSelectNothing) {
  EXPECT_TRUE(matchScreens({QStringLiteral("all")}, {}).empty());
}
