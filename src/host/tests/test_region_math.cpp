// Region math: the pure membership test behind a window's `mask`.
#include <gtest/gtest.h>

#include "host/region_math.h"

using namespace waku::host;

namespace {

RegionNode rect(qreal x, qreal y, qreal w, qreal h, RegionOp op = RegionOp::Combine) {
  RegionNode node;
  node.rect = QRectF(x, y, w, h);
  node.op = op;
  return node;
}

}  // namespace

TEST(RegionMathTest, ARectangleContainsItsInsideAndNotItsFarEdges) {
  const RegionNode r = rect(10, 20, 30, 40);
  EXPECT_TRUE(regionContains(r, {10, 20}));
  EXPECT_TRUE(regionContains(r, {39.9, 59.9}));
  EXPECT_FALSE(regionContains(r, {40, 30}));
  EXPECT_FALSE(regionContains(r, {20, 60}));
  EXPECT_FALSE(regionContains(r, {9.9, 30}));
}

TEST(RegionMathTest, AnEmptyRegionContainsNothing) {
  EXPECT_FALSE(regionContains(RegionNode{}, {0, 0}));
  EXPECT_FALSE(regionContains(rect(5, 5, 0, 10), {5, 6}));
  EXPECT_FALSE(regionContains(rect(5, 5, 10, -3), {6, 4}));
}

TEST(RegionMathTest, AnEllipseContainsItsCentreNotItsBoundingCorner) {
  RegionNode e = rect(0, 0, 100, 50);
  e.shape = RegionShapeKind::Ellipse;
  EXPECT_TRUE(regionContains(e, {50, 25}));
  EXPECT_TRUE(regionContains(e, {5, 25}));
  EXPECT_FALSE(regionContains(e, {2, 2}));
}

TEST(RegionMathTest, CombineIsTheUnion) {
  RegionNode root = rect(0, 0, 10, 10);
  root.children << rect(20, 0, 10, 10, RegionOp::Combine);
  EXPECT_TRUE(regionContains(root, {5, 5}));
  EXPECT_TRUE(regionContains(root, {25, 5}));
  EXPECT_FALSE(regionContains(root, {15, 5}));
}

TEST(RegionMathTest, IntersectKeepsTheOverlap) {
  RegionNode root = rect(0, 0, 20, 20);
  root.children << rect(10, 10, 20, 20, RegionOp::Intersect);
  EXPECT_TRUE(regionContains(root, {15, 15}));
  EXPECT_FALSE(regionContains(root, {5, 5}));
  EXPECT_FALSE(regionContains(root, {25, 25}));
}

TEST(RegionMathTest, XorKeepsWhatIsInExactlyOne) {
  RegionNode root = rect(0, 0, 20, 20);
  root.children << rect(10, 10, 20, 20, RegionOp::Xor);
  EXPECT_TRUE(regionContains(root, {5, 5}));
  EXPECT_TRUE(regionContains(root, {25, 25}));
  EXPECT_FALSE(regionContains(root, {15, 15}));
}

TEST(RegionMathTest, SubtractCutsAHole) {
  RegionNode root = rect(0, 0, 100, 100);
  root.children << rect(40, 40, 20, 20, RegionOp::Subtract);
  EXPECT_TRUE(regionContains(root, {10, 10}));
  EXPECT_FALSE(regionContains(root, {50, 50}));
}

TEST(RegionMathTest, ChildrenApplyInOrder) {
  // Cut a hole, then add a rectangle back inside it.
  RegionNode root = rect(0, 0, 100, 100);
  root.children << rect(20, 20, 60, 60, RegionOp::Subtract) << rect(40, 40, 10, 10);
  EXPECT_FALSE(regionContains(root, {30, 30}));
  EXPECT_TRUE(regionContains(root, {45, 45}));
  // The other order: the cut removes the added rectangle.
  RegionNode reversed = rect(0, 0, 100, 100);
  reversed.children << rect(40, 40, 10, 10) << rect(20, 20, 60, 60, RegionOp::Subtract);
  EXPECT_FALSE(regionContains(reversed, {45, 45}));
}

TEST(RegionMathTest, NestedRegionsFoldInnerFirst) {
  // a is 0..50 with a hole at 20..30; the root is 25..75 and xors with a.
  RegionNode a = rect(0, 0, 50, 50, RegionOp::Xor);
  a.children << rect(20, 20, 10, 10, RegionOp::Subtract);
  RegionNode root = rect(25, 25, 50, 50);
  root.children << a;
  EXPECT_TRUE(regionContains(root, {5, 5}));     // only in a
  EXPECT_FALSE(regionContains(root, {22, 22}));  // in a's hole, outside the root
  EXPECT_TRUE(regionContains(root, {27, 27}));   // in the root and in a's hole
  EXPECT_FALSE(regionContains(root, {30, 30}));  // in the root and in a
  EXPECT_TRUE(regionContains(root, {60, 60}));   // only in the root
}

TEST(RegionMathTest, ACelesteStyleMaskIsTheWindowMinusItsPanels) {
  RegionNode root = rect(40, 8, 960, 700, RegionOp::Combine);
  root.children << rect(300, 8, 200, 100, RegionOp::Subtract)
                << rect(800, 400, 200, 100, RegionOp::Subtract);
  EXPECT_TRUE(regionContains(root, {100, 300}));
  EXPECT_FALSE(regionContains(root, {350, 50}));
  EXPECT_FALSE(regionContains(root, {900, 450}));
  EXPECT_FALSE(regionContains(root, {10, 300}));
}

TEST(RegionMathTest, TheRootCombineIgnoresTheWindow) {
  const RegionNode root = rect(10, 10, 20, 20, RegionOp::Combine);
  const QSizeF window(100, 100);
  EXPECT_TRUE(regionContainsInWindow(root, {15, 15}, window));
  EXPECT_FALSE(regionContainsInWindow(root, {50, 50}, window));
}

TEST(RegionMathTest, TheRootXorIsTheWindowMinusTheRegion) {
  const RegionNode root = rect(10, 10, 20, 20, RegionOp::Xor);
  const QSizeF window(100, 100);
  EXPECT_FALSE(regionContainsInWindow(root, {15, 15}, window));
  EXPECT_TRUE(regionContainsInWindow(root, {50, 50}, window));
  EXPECT_FALSE(regionContainsInWindow(root, {150, 50}, window));  // outside the window
}

TEST(RegionMathTest, TheRootSubtractIsTheWindowMinusTheRegion) {
  const RegionNode root = rect(10, 10, 20, 20, RegionOp::Subtract);
  const QSizeF window(100, 100);
  EXPECT_FALSE(regionContainsInWindow(root, {15, 15}, window));
  EXPECT_TRUE(regionContainsInWindow(root, {50, 50}, window));
}

TEST(RegionMathTest, TheRootIntersectClipsToTheWindow) {
  const RegionNode root = rect(80, 80, 60, 60, RegionOp::Intersect);
  const QSizeF window(100, 100);
  EXPECT_TRUE(regionContainsInWindow(root, {90, 90}, window));
  EXPECT_FALSE(regionContainsInWindow(root, {120, 120}, window));
}

TEST(RegionMathTest, ACelesteRootXorIsTheBarBordersAndOpenPanels) {
  // The window is 1000x700. The root is the inner area (40, 8, 952, 684) xor-ed with the window; a
  // panel (300, 8, 200, 100) is cut out of it (an open drawer). Input is the bar, the borders and
  // the panel; not the inner area outside the panel.
  RegionNode root = rect(40, 8, 952, 684, RegionOp::Xor);
  root.children << rect(300, 8, 200, 100, RegionOp::Subtract);
  const QSizeF window(1000, 700);
  EXPECT_TRUE(regionContainsInWindow(root, {10, 300}, window));    // the bar
  EXPECT_TRUE(regionContainsInWindow(root, {500, 3}, window));     // the top border
  EXPECT_TRUE(regionContainsInWindow(root, {996, 300}, window));   // the right border
  EXPECT_TRUE(regionContainsInWindow(root, {400, 50}, window));    // inside the panel
  EXPECT_FALSE(regionContainsInWindow(root, {600, 300}, window));  // the inner area
  EXPECT_TRUE(maskAcceptsInput(std::optional<RegionNode>(root), {400, 50}, 6, window));
  EXPECT_FALSE(maskAcceptsInput(std::optional<RegionNode>(root), {600, 300}, 6, window));
  // The pad still grows the accepted area by about 6 px.
  EXPECT_TRUE(maskAcceptsInput(std::optional<RegionNode>(root), {600, 8 + 5}, 6, window));
}

TEST(RegionMathTest, PaddingGrowsTheRegion) {
  const RegionNode r = rect(100, 100, 50, 50);
  EXPECT_FALSE(regionContains(r, {96, 120}));
  EXPECT_TRUE(regionContainsPadded(r, {96, 120}, 6));
  EXPECT_TRUE(regionContainsPadded(r, {120, 155}, 6));
  EXPECT_FALSE(regionContainsPadded(r, {90, 120}, 6));
  EXPECT_FALSE(regionContainsPadded(r, {96, 120}, 0));
}

TEST(RegionMathTest, PaddingNeverFillsAnEmptyRegion) {
  EXPECT_FALSE(regionContainsPadded(RegionNode{}, {0, 0}, 6));
  EXPECT_FALSE(regionContainsPadded(rect(0, 0, 0, 0), {1, 1}, 6));
}

TEST(RegionMathTest, NoMaskAcceptsInputEverywhere) {
  EXPECT_TRUE(maskAcceptsInput(std::nullopt, {-5000, 99999}, 6));
}

TEST(RegionMathTest, AMaskAcceptsInputOnlyInsideTheRegion) {
  const std::optional<RegionNode> mask = rect(0, 0, 10, 10);
  EXPECT_TRUE(maskAcceptsInput(mask, {5, 5}, 6));
  EXPECT_FALSE(maskAcceptsInput(mask, {50, 50}, 6));
  EXPECT_FALSE(maskAcceptsInput(std::optional<RegionNode>(RegionNode{}), {0, 0}, 6));
}

TEST(RegionMathTest, ASecondaryScreenAtDprTwoKeepsItsNativeOriginUnscaled) {
  // A 200% screen right of a 100% primary, raised 200 px: native origin (3840, -200). Qt reports
  // its origin in native pixels and its size in logical ones, so the physical point
  // (3840 + 400, -200 + 300) is 200 and 150 logical pixels into the screen. The window sits at
  // logical (3840 + 100, -200 + 50).
  const QPointF p =
      cursorInWindow({3840 + 400, -200 + 300}, {3840, -200}, 2.0, {3840 + 100, -200 + 50});
  EXPECT_DOUBLE_EQ(p.x(), 100);
  EXPECT_DOUBLE_EQ(p.y(), 100);
  // The cursor on the window's top-left corner is the origin of the window.
  const QPointF corner =
      cursorInWindow({3840 + 200, -200 + 100}, {3840, -200}, 2.0, {3840 + 100, -200 + 50});
  EXPECT_DOUBLE_EQ(corner.x(), 0);
  EXPECT_DOUBLE_EQ(corner.y(), 0);
  // A half logical pixel survives the conversion.
  const QPointF half =
      cursorInWindow({3840 + 201, -200 + 100}, {3840, -200}, 2.0, {3840 + 100, -200 + 50});
  EXPECT_DOUBLE_EQ(half.x(), 0.5);
}

TEST(RegionMathTest, CursorConversionScalesFromTheScreenOrigin) {
  // A 150% screen whose native origin is (1920, 0): window at logical (1920 + 100, 50).
  const QPointF p = cursorInWindow({1920 + 300, 150}, {1920, 0}, 1.5, {1920 + 100, 50});
  EXPECT_DOUBLE_EQ(p.x(), 100);
  EXPECT_DOUBLE_EQ(p.y(), 50);
  const QPointF same = cursorInWindow({30, 40}, {0, 0}, 1.0, {10, 10});
  EXPECT_DOUBLE_EQ(same.x(), 20);
  EXPECT_DOUBLE_EQ(same.y(), 30);
  // A bad ratio is treated as 1.
  EXPECT_DOUBLE_EQ(cursorInWindow({10, 10}, {0, 0}, 0, {0, 0}).x(), 10);
}
