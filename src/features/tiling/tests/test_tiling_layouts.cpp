// The pure geometry of the tiler: rects, border compensation, layout names.
#include <gtest/gtest.h>

#include <QSet>

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
  for (const QString& layout : waku::tiling::layoutNames()) {
    for (int n = 1; n <= 12; ++n) {
      const auto rects = layoutSlots(layout, area, n, 6, 0.6);
      EXPECT_EQ(rects.size(), static_cast<size_t>(n)) << qPrintable(layout);
      expectDisjointInside(rects, area);
    }
  }
}

TEST(TilingLayouts, EveryLayoutKeepsItsInvariantsAtEveryCountAndSize) {
  const QRect areas[] = {QRect(0, 0, 1920, 1040), QRect(-1920, 40, 1366, 728), QRect(5, 7, 80, 60)};
  for (const QString& layout : waku::tiling::layoutNames()) {
    for (const QRect& area : areas) {
      for (const int gap : {0, 8, 40}) {
        for (const double ratio : {0.1, 0.5, 0.77, 0.9}) {
          for (int n = 1; n <= 9; ++n) {
            const auto rects = layoutSlots(layout, area, n, gap, ratio);
            SCOPED_TRACE(qPrintable(QStringLiteral("%1 n=%2 gap=%3 ratio=%4 w=%5")
                                        .arg(layout)
                                        .arg(n)
                                        .arg(gap)
                                        .arg(ratio)
                                        .arg(area.width())));
            ASSERT_EQ(rects.size(), static_cast<size_t>(n)) << qPrintable(layout) << n;
            expectDisjointInside(rects, area);
          }
        }
      }
    }
  }
}

TEST(TilingLayouts, WithoutAGapTheSlotsTileTheAreaExactly) {
  const QRect area(0, 0, 1001, 603);  // odd sizes: the last slot absorbs the rounding
  for (const QString& layout : waku::tiling::layoutNames()) {
    for (int n = 1; n <= 9; ++n) {
      qint64 sum = 0;
      for (const QRect& r : layoutSlots(layout, area, n, 0, 0.5))
        sum += qint64{r.width()} * r.height();
      EXPECT_EQ(sum, qint64{area.width()} * area.height()) << qPrintable(layout) << n;
    }
  }
}

TEST(TilingLayouts, MainStackTopPutsTheMainWindowOnTopWithTheRestBelow) {
  const QRect area(0, 0, 1000, 600);
  const auto rects = layoutSlots(QStringLiteral("mainStackTop"), area, 3, 0, 0.5);
  ASSERT_EQ(rects.size(), 3u);
  EXPECT_EQ(rects[0], QRect(0, 0, 1000, 300));
  EXPECT_EQ(rects[1], QRect(0, 300, 500, 300));
  EXPECT_EQ(rects[2], QRect(500, 300, 500, 300));
  EXPECT_EQ(layoutSlots(QStringLiteral("mainStackTop"), area, 2, 0, 0.75)[0].height(), 450);
}

TEST(TilingLayouts, RowsAreEqualBandsTopToBottom) {
  const QRect area(0, 0, 1000, 600);
  const auto rects = layoutSlots(QStringLiteral("rows"), area, 3, 0, 0.5);
  ASSERT_EQ(rects.size(), 3u);
  EXPECT_EQ(rects[0], QRect(0, 0, 1000, 200));
  EXPECT_EQ(rects[1], QRect(0, 200, 1000, 200));
  EXPECT_EQ(rects[2], QRect(0, 400, 1000, 200));
}

TEST(TilingLayouts, BspHalvesTheAreaThenSplitsWhatIsLeftAlternately) {
  const QRect area(0, 0, 1000, 600);
  const auto rects = layoutSlots(QStringLiteral("bsp"), area, 4, 0, 0.5);
  ASSERT_EQ(rects.size(), 4u);
  EXPECT_EQ(rects[0], QRect(0, 0, 500, 600));    // left half
  EXPECT_EQ(rects[1], QRect(500, 0, 500, 300));  // top of the right half
  EXPECT_EQ(rects[2], QRect(500, 300, 250, 300));
  EXPECT_EQ(rects[3], QRect(750, 300, 250, 300));
  // The ratio drives the first split only.
  EXPECT_EQ(layoutSlots(QStringLiteral("bsp"), area, 3, 0, 0.75)[0].width(), 750);
  EXPECT_EQ(layoutSlots(QStringLiteral("bsp"), area, 3, 0, 0.75)[1].height(), 300);
}

TEST(TilingLayouts, GridIsNearSquareAndTheLastRowSpreadsOverTheWidth) {
  const QRect area(0, 0, 900, 600);
  const auto four = layoutSlots(QStringLiteral("grid"), area, 4, 0, 0.5);
  EXPECT_EQ(four[0], QRect(0, 0, 450, 300));
  EXPECT_EQ(four[3], QRect(450, 300, 450, 300));
  const auto five = layoutSlots(QStringLiteral("grid"), area, 5, 0, 0.5);  // 3 + 2
  EXPECT_EQ(five[2], QRect(600, 0, 300, 300));
  EXPECT_EQ(five[3], QRect(0, 300, 450, 300));
  EXPECT_EQ(five[4], QRect(450, 300, 450, 300));
  const auto two = layoutSlots(QStringLiteral("grid"), area, 2, 0, 0.5);
  EXPECT_EQ(two[1], QRect(450, 0, 450, 600));
}

TEST(TilingLayouts, CenterMainPutsTheMainWindowInTheMiddleBetweenTwoStacks) {
  const QRect area(0, 0, 1000, 600);
  const auto rects = layoutSlots(QStringLiteral("centerMain"), area, 5, 0, 0.5);
  ASSERT_EQ(rects.size(), 5u);
  EXPECT_EQ(rects[0], QRect(250, 0, 500, 600));
  EXPECT_EQ(rects[1], QRect(750, 0, 250, 300));  // right stack first
  EXPECT_EQ(rects[2], QRect(0, 0, 250, 300));
  EXPECT_EQ(rects[3], QRect(750, 300, 250, 300));
  EXPECT_EQ(rects[4], QRect(0, 300, 250, 300));
  // Three windows: one column on each side, full height.
  const auto three = layoutSlots(QStringLiteral("centerMain"), area, 3, 0, 0.5);
  EXPECT_EQ(three[1], QRect(750, 0, 250, 600));
  EXPECT_EQ(three[2], QRect(0, 0, 250, 600));
  // Two windows are main and stack.
  EXPECT_EQ(layoutSlots(QStringLiteral("centerMain"), area, 2, 8, 0.5),
            layoutSlots(QStringLiteral("mainStack"), area, 2, 8, 0.5));
}

TEST(TilingLayouts, FlipMirrorsEveryLayout) {
  const QRect area(100, 50, 1000, 600);
  for (const QString& layout : waku::tiling::layoutNames()) {
    for (int n = 1; n <= 7; ++n) {
      const auto rects = layoutSlots(layout, area, n, 8, 0.6);
      const auto h = waku::tiling::flipSlots(rects, area, true, false);
      const auto v = waku::tiling::flipSlots(rects, area, false, true);
      expectDisjointInside(h, area);
      expectDisjointInside(v, area);
      for (size_t i = 0; i < rects.size(); ++i) {
        EXPECT_EQ(h[i].size(), rects[i].size());
        EXPECT_EQ(h[i].left() - area.left(), area.right() - rects[i].right()) << qPrintable(layout);
        EXPECT_EQ(h[i].top(), rects[i].top());
        EXPECT_EQ(v[i].top() - area.top(), area.bottom() - rects[i].bottom());
        EXPECT_EQ(v[i].left(), rects[i].left());
      }
      EXPECT_EQ(waku::tiling::flipSlots(h, area, true, false), rects);
    }
  }
}

TEST(TilingLayouts, TheLayoutListStartsWithMainStackAndHasNoDuplicates) {
  const QStringList names = waku::tiling::layoutNames();
  EXPECT_EQ(names.first(), QStringLiteral("mainStack"));
  EXPECT_EQ(names.size(), 7);
  EXPECT_EQ(QSet<QString>(names.begin(), names.end()).size(), names.size());
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
  EXPECT_EQ(canonicalLayout(QStringLiteral("main-stack-top")), QStringLiteral("mainStackTop"));
  EXPECT_EQ(canonicalLayout(QStringLiteral("BSP")), QStringLiteral("bsp"));
  EXPECT_EQ(canonicalLayout(QStringLiteral("center_main")), QStringLiteral("centerMain"));
  EXPECT_EQ(canonicalLayout(QStringLiteral("GRID")), QStringLiteral("grid"));
  EXPECT_EQ(canonicalLayout(QStringLiteral("Rows")), QStringLiteral("rows"));
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

TEST(TilingLayouts, FlipMirrorsTheSlotsInsideTheArea) {
  const QRect area(0, 0, 1000, 600);
  const auto rects = layoutSlots(QStringLiteral("mainStack"), area, 3, 10, 0.5);
  const auto flipped = waku::tiling::flipSlots(rects, area, true, false);
  ASSERT_EQ(flipped.size(), 3u);
  EXPECT_LT(flipped[1].left(), flipped[0].left());  // the main window is on the right now
  EXPECT_EQ(flipped[0].width(), rects[0].width());
  EXPECT_EQ(flipped[0].top(), rects[0].top());
  const auto twice = waku::tiling::flipSlots(flipped, area, true, false);
  EXPECT_EQ(twice, rects);
  const auto upside = waku::tiling::flipSlots(rects, area, false, true);
  EXPECT_GT(upside[1].top(), upside[2].top());  // the stack runs bottom to top
  expectDisjointInside(flipped, area);
  expectDisjointInside(upside, area);
}

TEST(TilingLayouts, WeightsSizeTheStackedWindowsByHeight) {
  const QRect area(0, 0, 1000, 900);
  const auto rects = layoutSlots(QStringLiteral("mainStack"), area, 3, 10, 0.5, {1.0, 2.0, 1.0});
  ASSERT_EQ(rects.size(), 3u);
  EXPECT_GT(rects[1].height(), rects[2].height());
  EXPECT_EQ(rects[2].bottom(), 889);
  expectDisjointInside(rects, area);
  // Equal weights are the plain layout.
  EXPECT_EQ(layoutSlots(QStringLiteral("mainStack"), area, 3, 10, 0.5, {1.0, 1.0, 1.0}),
            layoutSlots(QStringLiteral("mainStack"), area, 3, 10, 0.5));
}

TEST(TilingLayouts, NeighbourSlotFindsTheNearestSlotInADirection) {
  const QRect area(0, 0, 1000, 900);
  const auto rects = layoutSlots(QStringLiteral("mainStack"), area, 4, 10, 0.5);
  using waku::tiling::Direction;
  using waku::tiling::neighbourSlot;
  EXPECT_EQ(neighbourSlot(rects, 0, Direction::Right), 2u);  // the middle of the stack
  EXPECT_EQ(neighbourSlot(rects, 1, Direction::Left), 0u);
  EXPECT_EQ(neighbourSlot(rects, 1, Direction::Down), 2u);
  EXPECT_EQ(neighbourSlot(rects, 3, Direction::Up), 2u);
  EXPECT_FALSE(neighbourSlot(rects, 0, Direction::Left).has_value());
  EXPECT_FALSE(neighbourSlot(rects, 1, Direction::Up).has_value());
  EXPECT_FALSE(neighbourSlot(rects, 9, Direction::Up).has_value());
}
