#include <gtest/gtest.h>

#include "host/app_bar.h"
#include "host/testing/app_bar_fakes.h"

using wabar::host::AppBarEdge;
using wabar::host::AppBarGrant;
using wabar::host::AppBarRect;
using wabar::host::appBarWindowShift;
using wabar::host::computeAppBarRect;
using wabar::host::FakeAppBarSystem;
using wabar::host::negotiateAppBarRect;
using wabar::host::physicalScreenRect;

namespace {

// A 1920x1080 screen at 100%.
const QRect kScreen(0, 0, 1920, 1080);
const AppBarRect kPhysical{0, 0, 1920, 1080};

}  // namespace

TEST(AppBarEdgeTest, TheValuesAreTheAbeConstants) {
  EXPECT_EQ(static_cast<int>(AppBarEdge::Left), 0);
  EXPECT_EQ(static_cast<int>(AppBarEdge::Top), 1);
  EXPECT_EQ(static_cast<int>(AppBarEdge::Right), 2);
  EXPECT_EQ(static_cast<int>(AppBarEdge::Bottom), 3);
}

// ---- the rect on each edge ----

TEST(AppBarRectTest, ATopBarTakesTheTopOfTheScreen) {
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Top, 40, kScreen, 1.0), (AppBarRect{0, 0, 1920, 40}));
}

TEST(AppBarRectTest, ABottomBarTakesTheBottomOfTheScreen) {
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Bottom, 40, kScreen, 1.0),
            (AppBarRect{0, 1040, 1920, 1080}));
}

TEST(AppBarRectTest, ALeftBarTakesTheLeftOfTheScreen) {
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Left, 48, kScreen, 1.0), (AppBarRect{0, 0, 48, 1080}));
}

TEST(AppBarRectTest, ARightBarTakesTheRightOfTheScreen) {
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Right, 48, kScreen, 1.0),
            (AppBarRect{1872, 0, 1920, 1080}));
}

TEST(AppBarRectTest, ARectIsInPhysicalPixelsOnBothAxesAtOneAndAHalf) {
  // 1920x1080 physical at 150% is 1280x720 logical; a 40 px logical bar is 60 physical.
  const QRect logical(0, 0, 1280, 720);
  EXPECT_EQ(physicalScreenRect(logical, 1.5), kPhysical);
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Top, 40, logical, 1.5), (AppBarRect{0, 0, 1920, 60}));
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Bottom, 40, logical, 1.5),
            (AppBarRect{0, 1020, 1920, 1080}));
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Left, 48, logical, 1.5), (AppBarRect{0, 0, 72, 1080}));
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Right, 48, logical, 1.5),
            (AppBarRect{1848, 0, 1920, 1080}));
}

TEST(AppBarRectTest, ASecondaryScreenKeepsItsNativeOrigin) {
  // Qt gives the origin in native desktop pixels and the size in device-independent pixels.
  const QRect secondary(1920, -200, 1280, 720);
  EXPECT_EQ(physicalScreenRect(secondary, 1.5), (AppBarRect{1920, -200, 3840, 880}));
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Right, 48, secondary, 1.5),
            (AppBarRect{3768, -200, 3840, 880}));
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Top, 40, secondary, 1.5),
            (AppBarRect{1920, -200, 3840, -140}));
}

TEST(AppBarRectTest, ThicknessRoundsToWholePhysicalPixels) {
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Top, 30, QRect(0, 0, 1536, 864), 1.25).height(), 38);
  EXPECT_EQ(computeAppBarRect(AppBarEdge::Left, 0, kScreen, 1.0).width(), 0);
}

// ---- the negotiation with the system ----

TEST(AppBarNegotiationTest, QueriesBeforeItSetsAndGrantsTheRequestedRectWhenNothingIsInTheWay) {
  FakeAppBarSystem system;
  const AppBarGrant grant = negotiateAppBarRect(system, AppBarEdge::Top, 40, kPhysical);
  ASSERT_EQ(system.calls.size(), 2u);
  EXPECT_STREQ(system.calls[0].what, "query");
  EXPECT_STREQ(system.calls[1].what, "set");
  EXPECT_EQ(grant.requested, (AppBarRect{0, 0, 1920, 40}));
  EXPECT_EQ(grant.granted, grant.requested);
  EXPECT_EQ(grant.inset, 0);
}

TEST(AppBarNegotiationTest, ATopBarMovesBelowAnotherBarAndKeepsItsThickness) {
  FakeAppBarSystem system;
  system.topLimit = 48;  // something already holds the top 48 pixels
  const AppBarGrant grant = negotiateAppBarRect(system, AppBarEdge::Top, 40, kPhysical);
  EXPECT_EQ(grant.granted, (AppBarRect{0, 48, 1920, 88}));
  EXPECT_EQ(grant.inset, 48);
  EXPECT_EQ(appBarWindowShift(AppBarEdge::Top, grant.inset), QPoint(0, 48));
  // SETPOS got the rect QUERYPOS adjusted, with the thickness re-applied.
  EXPECT_EQ(system.calls[1].rect, (AppBarRect{0, 48, 1920, 88}));
}

TEST(AppBarNegotiationTest, ABottomBarMovesAboveTheTaskbar) {
  FakeAppBarSystem system;
  system.bottomLimit = 1032;  // the Windows taskbar holds the bottom 48 pixels
  const AppBarGrant grant = negotiateAppBarRect(system, AppBarEdge::Bottom, 40, kPhysical);
  EXPECT_EQ(grant.granted, (AppBarRect{0, 992, 1920, 1032}));
  EXPECT_EQ(grant.inset, 48);
  EXPECT_EQ(appBarWindowShift(AppBarEdge::Bottom, grant.inset), QPoint(0, -48));
}

TEST(AppBarNegotiationTest, SideBarsMoveInwardFromTheirOwnEdge) {
  FakeAppBarSystem left;
  left.leftLimit = 70;
  const AppBarGrant l = negotiateAppBarRect(left, AppBarEdge::Left, 48, kPhysical);
  EXPECT_EQ(l.granted, (AppBarRect{70, 0, 118, 1080}));
  EXPECT_EQ(l.inset, 70);
  EXPECT_EQ(appBarWindowShift(AppBarEdge::Left, l.inset), QPoint(70, 0));

  FakeAppBarSystem right;
  right.rightLimit = 1900;
  const AppBarGrant r = negotiateAppBarRect(right, AppBarEdge::Right, 48, kPhysical);
  EXPECT_EQ(r.granted, (AppBarRect{1852, 0, 1900, 1080}));
  EXPECT_EQ(r.inset, 20);
  EXPECT_EQ(appBarWindowShift(AppBarEdge::Right, r.inset), QPoint(-20, 0));
}

TEST(AppBarNegotiationTest, TheRectSetPosReadsBackWins) {
  FakeAppBarSystem system;
  system.topLimit = 10;
  system.setExtra = 6;  // SETPOS pushes the bar further in than QUERYPOS did
  const AppBarGrant grant = negotiateAppBarRect(system, AppBarEdge::Top, 40, kPhysical);
  EXPECT_EQ(system.calls[0].rect, (AppBarRect{0, 0, 1920, 40}));
  EXPECT_EQ(system.calls[1].rect, (AppBarRect{0, 10, 1920, 50}));
  EXPECT_EQ(grant.granted, (AppBarRect{0, 16, 1920, 56}));
  EXPECT_EQ(grant.inset, 16);
}

TEST(AppBarNegotiationTest, AtOneAndAHalfOnASecondaryScreenTheRectsStayPhysical) {
  const QRect secondary(1920, 0, 1280, 720);
  FakeAppBarSystem system;
  system.rightLimit = 3800;
  const AppBarGrant grant =
      negotiateAppBarRect(system, AppBarEdge::Right, 72, physicalScreenRect(secondary, 1.5));
  EXPECT_EQ(grant.requested, (AppBarRect{3768, 0, 3840, 1080}));
  EXPECT_EQ(grant.granted, (AppBarRect{3728, 0, 3800, 1080}));
  EXPECT_EQ(grant.inset, 40);
}
