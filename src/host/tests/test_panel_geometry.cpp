#include <gtest/gtest.h>

#include "host/panel_geometry.h"

using waku::host::AppBarEdge;
using waku::host::coversScreenForFullscreen;
using waku::host::PanelAnchorSet;
using waku::host::panelAppBarEdge;
using waku::host::panelGeometry;
using waku::host::panelReservedZone;
using waku::host::panelThickness;

namespace {

const QRect kScreen(100, 50, 1000, 800);
const QSize kSize(200, 40);

PanelAnchorSet anchors(bool top, bool bottom, bool left, bool right) {
  return {top, bottom, left, right};
}

constexpr int kIgnore = 0;
constexpr int kNormal = 1;
constexpr int kAuto = 2;

}  // namespace

TEST(PanelGeometryTest, FullscreenCoversTheMonitorButACaptionedMaximizedWindowDoesNot) {
  EXPECT_TRUE(coversScreenForFullscreen(kScreen, kScreen, false, false));
  EXPECT_TRUE(coversScreenForFullscreen(kScreen, kScreen, true, false));
  EXPECT_FALSE(coversScreenForFullscreen(kScreen, kScreen, true, true));
  EXPECT_FALSE(coversScreenForFullscreen(kScreen.adjusted(0, 0, -1, 0), kScreen, false, false));
  EXPECT_FALSE(coversScreenForFullscreen(kScreen, QRect(), false, false));
}

TEST(PanelGeometryTest, ATopPanelSpansTheTopEdge) {
  EXPECT_EQ(panelGeometry(anchors(true, false, true, true), {}, kSize, kScreen),
            QRect(100, 50, 1000, 40));
}

TEST(PanelGeometryTest, ABottomPanelSitsOnTheBottomEdge) {
  EXPECT_EQ(panelGeometry(anchors(false, true, true, true), {}, kSize, kScreen),
            QRect(100, 50 + 800 - 40, 1000, 40));
}

TEST(PanelGeometryTest, LeftAndRightPanelsSpanTheirEdge) {
  EXPECT_EQ(panelGeometry(anchors(true, true, true, false), {}, kSize, kScreen),
            QRect(100, 50, 200, 800));
  EXPECT_EQ(panelGeometry(anchors(true, true, false, true), {}, kSize, kScreen),
            QRect(100 + 1000 - 200, 50, 200, 800));
}

TEST(PanelGeometryTest, AllFourAnchorsCoverTheScreen) {
  EXPECT_EQ(panelGeometry(anchors(true, true, true, true), {}, kSize, kScreen), kScreen);
}

TEST(PanelGeometryTest, NoAnchorsCentresTheWindow) {
  EXPECT_EQ(panelGeometry(anchors(false, false, false, false), {}, kSize, kScreen),
            QRect(100 + 400, 50 + 380, 200, 40));
}

TEST(PanelGeometryTest, OneEdgeKeepsTheImplicitSizeAndCentresAlongIt) {
  EXPECT_EQ(panelGeometry(anchors(true, false, false, false), {}, kSize, kScreen),
            QRect(100 + 400, 50, 200, 40));
}

TEST(PanelGeometryTest, MarginsMoveAndShrinkTheWindow) {
  const QMargins margins(10, 5, 20, 8);  // left, top, right, bottom
  EXPECT_EQ(panelGeometry(anchors(true, false, true, true), margins, kSize, kScreen),
            QRect(110, 55, 1000 - 30, 40));
  EXPECT_EQ(panelGeometry(anchors(false, true, false, true), margins, kSize, kScreen),
            QRect(100 + 1000 - 20 - 200, 50 + 800 - 8 - 40, 200, 40));
  EXPECT_EQ(panelGeometry(anchors(true, true, true, true), margins, kSize, kScreen),
            QRect(110, 55, 970, 787));
}

TEST(PanelGeometryTest, OneAnchoredEdgeWithTheCrossAxisStretchedOrCentredIsAnAppBar) {
  EXPECT_EQ(panelAppBarEdge(anchors(true, false, true, true)), AppBarEdge::Top);
  EXPECT_EQ(panelAppBarEdge(anchors(false, true, true, true)), AppBarEdge::Bottom);
  EXPECT_EQ(panelAppBarEdge(anchors(true, false, false, false)), AppBarEdge::Top);
  EXPECT_EQ(panelAppBarEdge(anchors(true, true, true, false)), AppBarEdge::Left);
  EXPECT_EQ(panelAppBarEdge(anchors(true, true, false, true)), AppBarEdge::Right);
  EXPECT_EQ(panelAppBarEdge(anchors(false, false, true, false)), AppBarEdge::Left);
  EXPECT_EQ(panelAppBarEdge(anchors(false, false, false, true)), AppBarEdge::Right);
}

TEST(PanelGeometryTest, ACornerABandAndAFullScreenWindowAreNotAppBars) {
  EXPECT_FALSE(panelAppBarEdge(anchors(true, false, true, false)).has_value());  // corner
  EXPECT_FALSE(panelAppBarEdge(anchors(false, true, false, true)).has_value());  // corner
  EXPECT_FALSE(panelAppBarEdge(anchors(true, true, false, false)).has_value());  // band
  EXPECT_FALSE(panelAppBarEdge(anchors(false, false, true, true)).has_value());  // band
  EXPECT_FALSE(panelAppBarEdge(anchors(true, true, true, true)).has_value());
  EXPECT_FALSE(panelAppBarEdge(anchors(false, false, false, false)).has_value());
}

TEST(PanelGeometryTest, ASidePanelSpansTheSideEdge) {
  const QSize size(48, 40);
  EXPECT_EQ(panelGeometry(anchors(true, true, true, false), {}, size, kScreen),
            QRect(100, 50, 48, 800));
  EXPECT_EQ(panelGeometry(anchors(true, true, false, true), {}, size, kScreen),
            QRect(100 + 1000 - 48, 50, 48, 800));
}

TEST(PanelGeometryTest, TheThicknessIsTheWidthOfASidePanel) {
  EXPECT_EQ(panelThickness(AppBarEdge::Top, QSize(1000, 40)), 40);
  EXPECT_EQ(panelThickness(AppBarEdge::Bottom, QSize(1000, 40)), 40);
  EXPECT_EQ(panelThickness(AppBarEdge::Left, QSize(48, 800)), 48);
  EXPECT_EQ(panelThickness(AppBarEdge::Right, QSize(48, 800)), 48);
}

TEST(PanelGeometryTest, TheReservedZoneFollowsTheExclusionMode) {
  const PanelAnchorSet top = anchors(true, false, true, true);
  EXPECT_EQ(panelReservedZone(top, {}, kIgnore, 30, kSize), 0);
  EXPECT_EQ(panelReservedZone(top, {}, kNormal, 30, kSize), 30);
  EXPECT_EQ(panelReservedZone(top, {}, kNormal, 0, kSize), 0);
  EXPECT_EQ(panelReservedZone(top, {}, kAuto, 0, kSize), 40);  // the window's own thickness
  EXPECT_EQ(panelReservedZone(top, QMargins(0, 6, 0, 0), kAuto, 0, kSize), 46);
  EXPECT_EQ(panelReservedZone(anchors(true, true, true, true), {}, kAuto, 0, kSize), 0);
}

TEST(PanelGeometryTest, TheReservedZoneOfASidePanelUsesItsWidthAndItsSideMargin) {
  const PanelAnchorSet left = anchors(true, true, true, false);
  const PanelAnchorSet right = anchors(true, true, false, true);
  const QSize size(48, 800);
  EXPECT_EQ(panelReservedZone(left, {}, kAuto, 0, size), 48);
  EXPECT_EQ(panelReservedZone(left, QMargins(6, 0, 0, 0), kAuto, 0, size), 54);
  EXPECT_EQ(panelReservedZone(right, QMargins(6, 0, 9, 0), kAuto, 0, size), 57);
  EXPECT_EQ(panelReservedZone(left, {}, kNormal, 30, size), 30);
  EXPECT_EQ(panelReservedZone(left, {}, kIgnore, 30, size), 0);
}
