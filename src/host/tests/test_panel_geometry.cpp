#include <gtest/gtest.h>

#include "host/panel_geometry.h"

using wabar::host::AppBarEdge;
using wabar::host::PanelAnchorSet;
using wabar::host::panelAppBarEdge;
using wabar::host::panelGeometry;
using wabar::host::panelReservedZone;

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

TEST(PanelGeometryTest, OnlyTopOrBottomAloneIsAnAppBar) {
  EXPECT_EQ(panelAppBarEdge(anchors(true, false, true, true)), AppBarEdge::Top);
  EXPECT_EQ(panelAppBarEdge(anchors(false, true, true, true)), AppBarEdge::Bottom);
  EXPECT_EQ(panelAppBarEdge(anchors(true, false, false, false)), AppBarEdge::Top);
  EXPECT_FALSE(panelAppBarEdge(anchors(true, true, true, true)).has_value());
  EXPECT_FALSE(panelAppBarEdge(anchors(true, true, true, false)).has_value());
  EXPECT_FALSE(panelAppBarEdge(anchors(false, false, false, false)).has_value());
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
