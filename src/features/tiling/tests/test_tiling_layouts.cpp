// The pure geometry of the tiler: rects, border compensation, layout names.
#include <gtest/gtest.h>

#include "features/tiling/layouts.h"

using waku::tiling::canonicalLayout;
using waku::tiling::layoutSlots;
using waku::tiling::outerRect;

namespace {

void expectDisjointInside(const std::vector<QRect>& rects, const QRect& area) {
  for (size_t i = 0; i < rects.size(); ++i) {
    EXPECT_TRUE(area.contains(rects[i])) << i;
    EXPECT_GT(rects[i].width(), 0) << i;
    EXPECT_GT(rects[i].height(), 0) << i;
    for (size_t j = i + 1; j < rects.size(); ++j) EXPECT_FALSE(rects[i].intersects(rects[j]));
  }
}

}  // namespace

TEST(TilingLayouts, OneWindowFillsTheAreaMinusTheGap) {
  const QRect area(0, 0, 1000, 600);
  const auto rects = layoutSlots(QStringLiteral("mainStack"), area, 1, 10, 0.5);
  ASSERT_EQ(rects.size(), 1u);
  EXPECT_EQ(rects[0], QRect(10, 10, 980, 580));
}

TEST(TilingLayouts, MainStackPutsTheMainWindowLeftAndStacksTheRest) {
  const QRect area(0, 0, 1000, 600);
  const auto rects = layoutSlots(QStringLiteral("mainStack"), area, 3, 10, 0.5);
  ASSERT_EQ(rects.size(), 3u);
  // Inner area 980x580 at (10,10); width without the middle gap 970, main 485.
  EXPECT_EQ(rects[0], QRect(10, 10, 485, 580));
  EXPECT_EQ(rects[1].x(), 505);
  EXPECT_EQ(rects[2].x(), 505);
  EXPECT_EQ(rects[1].width(), 485);
  EXPECT_EQ(rects[1].y(), 10);
  EXPECT_EQ(rects[2].y(), rects[1].y() + rects[1].height() + 10);
  EXPECT_EQ(rects[2].y() + rects[2].height(), 590);  // the last absorbs the remainder
  expectDisjointInside(rects, area);
}

TEST(TilingLayouts, RatioMovesTheSplitAndIsClamped) {
  const QRect area(0, 0, 1000, 600);
  const auto wide = layoutSlots(QStringLiteral("mainStack"), area, 2, 0, 0.75);
  EXPECT_EQ(wide[0].width(), 750);
  EXPECT_EQ(wide[1].x(), 750);
  const auto clamped = layoutSlots(QStringLiteral("mainStack"), area, 2, 0, 5.0);
  EXPECT_EQ(clamped[0].width(), 900);
}

TEST(TilingLayouts, ColumnsAreEqualAndAbsorbTheRemainder) {
  const QRect area(0, 0, 1000, 600);
  const auto rects = layoutSlots(QStringLiteral("columns"), area, 3, 8, 0.5);
  ASSERT_EQ(rects.size(), 3u);
  expectDisjointInside(rects, area);
  EXPECT_EQ(rects[0].y(), 8);
  EXPECT_EQ(rects[0].height(), 584);
  EXPECT_EQ(rects[2].x() + rects[2].width(), 992);
  EXPECT_NEAR(rects[0].width(), rects[1].width(), 1);
}

TEST(TilingLayouts, ManyWindowsStayInsideAndDisjoint) {
  const QRect area(1920, 0, 1280, 1000);
  for (const char* layout : {"mainStack", "columns"}) {
    for (int n = 1; n <= 12; ++n) {
      expectDisjointInside(layoutSlots(QString::fromLatin1(layout), area, n, 6, 0.6), area);
    }
  }
}

TEST(TilingLayouts, ZeroWindowsOrAnEmptyAreaGiveNothing) {
  EXPECT_TRUE(layoutSlots(QStringLiteral("mainStack"), QRect(0, 0, 100, 100), 0, 8, 0.5).empty());
  EXPECT_TRUE(layoutSlots(QStringLiteral("mainStack"), QRect(), 3, 8, 0.5).empty());
}

TEST(TilingLayouts, AHugeGapStillGivesPositiveSlots) {
  const QRect area(0, 0, 100, 100);
  const auto rects = layoutSlots(QStringLiteral("mainStack"), area, 4, 500, 0.5);
  ASSERT_EQ(rects.size(), 4u);
  expectDisjointInside(rects, area);
}

TEST(TilingLayouts, AnUnknownLayoutIsMainStack) {
  const QRect area(0, 0, 800, 600);
  EXPECT_EQ(layoutSlots(QStringLiteral("nonsense"), area, 3, 8, 0.5),
            layoutSlots(QStringLiteral("mainStack"), area, 3, 8, 0.5));
}

TEST(TilingLayouts, CanonicalNamesIgnoreCaseAndSeparators) {
  EXPECT_EQ(canonicalLayout(QStringLiteral("main-stack")), QStringLiteral("mainStack"));
  EXPECT_EQ(canonicalLayout(QStringLiteral("MAIN_STACK")), QStringLiteral("mainStack"));
  EXPECT_EQ(canonicalLayout(QStringLiteral("Columns")), QStringLiteral("columns"));
  EXPECT_TRUE(canonicalLayout(QStringLiteral("spiral")).isEmpty());
}

TEST(TilingLayouts, OuterRectGrowsTheSlotByTheInvisibleBorder) {
  // The window is 7 px wider on the left, right and bottom than what is drawn, 0 on top.
  const QRect window(93, 100, 654, 487);
  const QRect visible(100, 100, 640, 480);
  const QRect slot(10, 20, 500, 400);
  EXPECT_EQ(outerRect(slot, window, visible), QRect(3, 20, 514, 407));
}

TEST(TilingLayouts, OuterRectIsTheSlotWhenThereIsNoBorder) {
  const QRect rect(50, 50, 300, 200);
  EXPECT_EQ(outerRect(QRect(0, 0, 10, 10), rect, rect), QRect(0, 0, 10, 10));
}
