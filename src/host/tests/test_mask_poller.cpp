// MaskPoller with a fake cursor and a real (offscreen) window: when the click-through flag flips.
#include <gtest/gtest.h>

#include <QGuiApplication>
#include <QTest>
#include <QWindow>
#include <cmath>
#include <optional>

#include "host/mask_poller.h"

using namespace waku::host;

namespace {

RegionNode rect(qreal x, qreal y, qreal w, qreal h, RegionOp op = RegionOp::Combine) {
  RegionNode node;
  node.rect = QRectF(x, y, w, h);
  node.op = op;
  return node;
}

struct Fixture {
  QWindow window;
  std::optional<QPoint> cursor;
  std::optional<RegionNode> mask;
  qint64 now = 0;  // the hub's clock, milliseconds
  std::unique_ptr<MaskPollerHub> hub;
  std::unique_ptr<MaskPoller> poller;

  Fixture() {
    window.setGeometry(200, 100, 400, 300);
    cursor = QPoint(0, 0);
    mask = rect(0, 0, 100, 50);
    hub = std::make_unique<MaskPollerHub>([this] { return cursor; });
    hub->setClock([this] { return now; });
    poller = std::make_unique<MaskPoller>(&window, hub.get(), [this] { return mask; });
  }
  // The cursor at `x, y` in window coordinates.
  void at(int x, int y) { cursor = window.geometry().topLeft() + QPoint(x, y); }
  bool clickThrough() const { return window.flags().testFlag(Qt::WindowTransparentForInput); }
};

}  // namespace

TEST(MaskPollerTest, ItStaysIdleWhileTheWindowIsHidden) {
  Fixture f;
  f.poller->refresh();
  EXPECT_FALSE(f.poller->running());
  EXPECT_EQ(f.poller->flips(), 0);
}

TEST(MaskPollerTest, ItRunsWhileTheWindowIsVisibleAndMasked) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  EXPECT_TRUE(f.poller->running());
}

TEST(MaskPollerTest, ItDoesNotRunWithoutAMask) {
  Fixture f;
  f.mask.reset();
  f.window.show();
  f.poller->refresh();
  EXPECT_FALSE(f.poller->running());
  EXPECT_FALSE(f.clickThrough());
}

TEST(MaskPollerTest, ACursorOutsideTheRegionMakesTheWindowClickThrough) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  EXPECT_TRUE(f.poller->poll());
  EXPECT_TRUE(f.clickThrough());
}

TEST(MaskPollerTest, ACursorInsideTheRegionTakesInput) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  f.poller->poll();
  f.at(50, 25);
  EXPECT_FALSE(f.poller->poll());
  EXPECT_FALSE(f.clickThrough());
}

TEST(MaskPollerTest, TheFlagChangesOnlyWhenTheAnswerChanges) {
  Fixture f;
  f.window.show();
  f.poller->refresh();  // cursor at the screen origin: outside the region -> one flip
  const int start = f.poller->flips();
  f.at(300, 200);
  for (int i = 0; i < 20; ++i) f.poller->poll();
  EXPECT_EQ(f.poller->flips(), start);
  f.at(10, 10);
  for (int i = 0; i < 20; ++i) f.poller->poll();
  EXPECT_EQ(f.poller->flips(), start + 1);
  f.at(11, 10);
  f.poller->poll();
  EXPECT_EQ(f.poller->flips(), start + 1);
}

TEST(MaskPollerTest, ThePadTakesInputJustBeforeTheRegion) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(100 + 5, 25);  // 5 px outside the right edge
  EXPECT_FALSE(f.poller->poll());
  f.at(100 + 7, 25);  // beyond the 6 px pad
  EXPECT_TRUE(f.poller->poll());
}

TEST(MaskPollerTest, AnEmptyRegionIsFullyClickThrough) {
  Fixture f;
  f.mask = RegionNode{};
  f.window.show();
  f.poller->refresh();
  for (const QPoint p : {QPoint(0, 0), QPoint(50, 25), QPoint(399, 299)}) {
    f.at(p.x(), p.y());
    EXPECT_TRUE(f.poller->poll());
  }
  EXPECT_TRUE(f.clickThrough());
}

TEST(MaskPollerTest, HidingTheWindowStopsThePoll) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  ASSERT_TRUE(f.poller->running());
  f.window.hide();
  f.poller->poll();
  EXPECT_FALSE(f.poller->running());
}

TEST(MaskPollerTest, RemovingTheMaskStopsThePollAndRestoresInput) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  f.poller->poll();
  ASSERT_TRUE(f.clickThrough());
  f.mask.reset();
  f.poller->poll();
  EXPECT_FALSE(f.poller->running());
  EXPECT_FALSE(f.clickThrough());
}

TEST(MaskPollerTest, AnUnreadableCursorChangesNothing) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  f.poller->poll();
  const int flips = f.poller->flips();
  f.cursor.reset();
  f.poller->poll();
  EXPECT_EQ(f.poller->flips(), flips);
  EXPECT_TRUE(f.poller->running());
}

TEST(MaskPollerTest, TheHubRunsAt120HzWhileTheCursorMovesAnd30HzWhenItRests) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  ASSERT_TRUE(f.hub->running());
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kFastIntervalMs);
  EXPECT_EQ(MaskPollerHub::kFastIntervalMs, 8);
  EXPECT_EQ(MaskPollerHub::kIdleIntervalMs, 33);
  // The cursor moves near the region: still fast 299 ms later, idle after 300 ms without movement.
  f.at(110, 25);
  f.now = 1000;
  f.hub->tick();
  f.now = 1299;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kFastIntervalMs);
  f.now = 1301;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kIdleIntervalMs);
  // Movement brings the fast rate back at once.
  f.at(111, 25);
  f.now = 1340;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kFastIntervalMs);
}

TEST(MaskPollerTest, OneTimerServesEveryMaskedWindow) {
  Fixture f;
  QWindow second;
  second.setGeometry(0, 0, 300, 300);
  QWindow third;
  third.setGeometry(500, 500, 300, 300);
  MaskPoller p2(&second, f.hub.get(), [] { return std::optional<RegionNode>(rect(0, 0, 10, 10)); });
  MaskPoller p3(&third, f.hub.get(), [] { return std::optional<RegionNode>(rect(0, 0, 10, 10)); });
  f.window.show();
  second.show();
  third.show();
  f.poller->refresh();
  p2.refresh();
  p3.refresh();
  EXPECT_EQ(f.hub->pollerCount(), 3);
  EXPECT_TRUE(f.hub->running());
  const int reads = f.hub->cursorReads();
  f.at(300, 200);
  f.hub->tick();
  EXPECT_EQ(f.hub->cursorReads(), reads + 1);  // one cursor read per tick, not one per window
  second.hide();
  third.hide();
  f.hub->tick();
  EXPECT_EQ(f.hub->pollerCount(), 1);
  f.window.hide();
  f.hub->tick();
  EXPECT_EQ(f.hub->pollerCount(), 0);
  EXPECT_FALSE(f.hub->running());
}

TEST(MaskPollerTest, ARestingCursorSkipsTheEvaluation) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  f.now = 10;
  f.hub->tick();  // moved: evaluated
  const int evaluations = f.hub->evaluations();
  for (int i = 1; i <= 10; ++i) {
    f.now = 10 + i * 33;
    f.hub->tick();
  }
  EXPECT_EQ(f.hub->evaluations(), evaluations);
  EXPECT_TRUE(f.hub->cursorReads() > 0);
}

TEST(MaskPollerTest, ARestingCursorIsTestedAgainWhenSomethingChanged) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  f.now = 10;
  f.hub->tick();
  ASSERT_TRUE(f.clickThrough());
  const int evaluations = f.hub->evaluations();
  // The region grows over the resting cursor, and the poller is told (a frame, a region change).
  f.mask = rect(0, 0, 400, 300);
  f.poller->markDirty();
  f.now = 43;
  f.hub->tick();
  EXPECT_EQ(f.hub->evaluations(), evaluations + 1);
  EXPECT_FALSE(f.clickThrough());
  // The window moves under the resting cursor: no signal needed.
  f.window.setGeometry(1000, 100, 400, 300);
  f.now = 76;
  f.hub->tick();
  EXPECT_EQ(f.hub->evaluations(), evaluations + 2);
  // And a poller is evaluated at least once a second.
  f.now = 76 + MaskPollerHub::kRecheckMs;
  f.hub->tick();
  EXPECT_EQ(f.hub->evaluations(), evaluations + 3);
}

TEST(MaskPollerTest, TheFlagFlipsOnTheFirstTickAfterTheCursorMovesFromRest) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  f.now = 10;
  f.hub->tick();
  f.now = 2000;
  f.hub->tick();  // long at rest: the idle rate
  ASSERT_EQ(f.hub->intervalMs(), MaskPollerHub::kIdleIntervalMs);
  ASSERT_TRUE(f.clickThrough());
  f.at(50, 25);  // into the region
  f.now = 2033;
  f.hub->tick();  // one tick is enough
  EXPECT_FALSE(f.clickThrough());
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kFastIntervalMs);
}

TEST(MaskPollerTest, TheEdgeDistanceBoundsWhereTheAnswerCanChange) {
  const RegionNode r = rect(0, 0, 100, 50);
  EXPECT_DOUBLE_EQ(regionEdgeDistance(r, {150, 25}), 50);  // outside, beside
  EXPECT_DOUBLE_EQ(regionEdgeDistance(r, {103, 54}), 5);   // outside, off the corner
  EXPECT_DOUBLE_EQ(regionEdgeDistance(r, {50, 20}), 20);   // inside: the nearest side
  RegionNode tree = rect(0, 0, 100, 50);
  tree.children.append(rect(300, 0, 10, 10));
  tree.children.append(rect(140, 0, 0, 10));  // no area: never contains anything
  EXPECT_DOUBLE_EQ(regionEdgeDistance(tree, {290, 5}), 10);
  RegionNode ellipse = rect(0, 0, 100, 100);
  ellipse.shape = RegionShapeKind::Ellipse;
  EXPECT_DOUBLE_EQ(regionEdgeDistance(ellipse, {10, 10}), 0);  // the curve may be nearer
  EXPECT_DOUBLE_EQ(regionEdgeDistance(ellipse, {130, 50}), 30);
  // Against the window too when the root op uses it.
  RegionNode xorMask = rect(500, 500, 10, 10, RegionOp::Xor);
  EXPECT_DOUBLE_EQ(regionEdgeDistance(xorMask, {20, 300}, QSizeF(800, 600)), 20);
  EXPECT_DOUBLE_EQ(regionEdgeDistance(xorMask, {20, 300}), std::hypot(480.0, 200.0));
  EXPECT_TRUE(std::isinf(regionEdgeDistance(RegionNode{}, {0, 0})));
}

TEST(MaskPollerTest, AFarCursorSlowsTheHubDown) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  // Far from the only region: the slowest rate, moving or not.
  f.at(6000, 25);
  f.now = 1000;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kSlowIntervalMs);
  f.now = 3000;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kSlowIntervalMs);
  // 1007 px from the padded edge: as long as a cursor at the top speed needs to get there.
  f.at(100 + 1007 + 7, 25);
  f.now = 3250;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), 1007 / static_cast<int>(MaskPollerHub::kMaxCursorSpeed));
  // Near it: the fast rate again.
  f.at(110, 25);
  f.now = 3300;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kFastIntervalMs);
  // An unreadable cursor gives no distance: the near rates.
  f.at(3000, 25);
  f.now = 3310;
  f.hub->tick();
  f.cursor.reset();
  f.now = 4000;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kIdleIntervalMs);
}

TEST(MaskPollerTest, TheNearestWindowSetsTheRate) {
  Fixture f;
  QWindow second;
  second.setGeometry(0, 0, 300, 300);
  MaskPoller p2(&second, f.hub.get(), [] { return std::optional<RegionNode>(rect(0, 0, 10, 10)); });
  f.window.show();
  second.show();
  f.poller->refresh();
  p2.refresh();
  f.cursor = QPoint(20, 20);  // 10 px from the second window's region, far from the first
  f.now = 1000;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kFastIntervalMs);
}

TEST(MaskPollerTest, AFrameBringsASlowTickForward) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(6000, 25);
  f.now = 1000;
  f.hub->tick();
  ASSERT_EQ(f.hub->intervalMs(), MaskPollerHub::kSlowIntervalMs);
  // At rest: the region may have grown under the cursor, test it within the idle rate.
  f.now = 2000;
  f.poller->markDirty();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kIdleIntervalMs);
  f.hub->tick();
  // While the cursor moves: within the fast rate.
  f.at(6001, 25);
  f.now = 2100;
  f.hub->tick();
  ASSERT_EQ(f.hub->intervalMs(), MaskPollerHub::kSlowIntervalMs);
  f.poller->markDirty();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kFastIntervalMs);
}

TEST(MaskPollerTest, AMovingCursorReusesTheMask) {
  Fixture f;
  f.window.show();
  f.poller->refresh();
  f.at(110, 25);
  f.now = 10;
  f.hub->tick();
  const int builds = f.poller->maskBuilds();
  const int evaluations = f.hub->evaluations();
  for (int i = 1; i <= 20; ++i) {
    f.at(110 + i, 25);
    f.now = 10 + i * 8;
    f.hub->tick();
  }
  EXPECT_EQ(f.hub->evaluations(), evaluations + 20);  // every move is tested
  EXPECT_EQ(f.poller->maskBuilds(), builds);          // against the same mask
  // A change builds it again: a dirty poller or a moved window. The once-a-second recheck tests
  // the cursor against the last mask but does not build it again.
  f.mask = rect(0, 0, 400, 300);
  f.poller->markDirty();
  f.now = 300;
  f.hub->tick();
  EXPECT_EQ(f.poller->maskBuilds(), builds + 1);
  EXPECT_FALSE(f.clickThrough());
  f.window.setGeometry(210, 100, 400, 300);
  f.now = 340;
  f.hub->tick();
  EXPECT_EQ(f.poller->maskBuilds(), builds + 2);
  const int evaluationsBefore = f.hub->evaluations();
  f.now = 340 + MaskPollerHub::kRecheckMs;
  f.hub->tick();
  EXPECT_EQ(f.hub->evaluations(), evaluationsBefore + 1);  // tested again
  EXPECT_EQ(f.poller->maskBuilds(), builds + 2);           // not built again
  for (int i = 1; i <= 5; ++i) {
    f.now = 340 + (i + 1) * MaskPollerHub::kRecheckMs;
    f.hub->tick();
  }
  EXPECT_EQ(f.poller->maskBuilds(), builds + 2);
}

// A cursor that comes from far away at the top speed, with the hub ticking at the intervals it
// picks: the window takes input no later than one fast tick after the cursor reaches the pad, as
// with a hub that ticks at 120 Hz all the time, but with far fewer ticks.
TEST(MaskPollerTest, AnApproachAtTopSpeedFlipsAsSoonAsAtTheFastRate) {
  Fixture f;
  f.mask = rect(0, 0, 1000, 50);  // wide enough that one fast tick cannot jump it
  f.window.show();
  f.poller->refresh();
  const qreal speed = MaskPollerHub::kMaxCursorSpeed;
  const int startX = 4000;
  const auto xAt = [&](qint64 t) { return startX - static_cast<int>(speed * t); };
  qint64 entered = -1;
  qint64 flipped = -1;
  int ticks = 0;
  for (f.now = 0; f.now < 400 && flipped < 0; f.now += f.hub->intervalMs()) {
    f.at(xAt(f.now), 25);
    f.hub->tick();
    ++ticks;
    if (!f.clickThrough()) flipped = f.now;
  }
  for (qint64 t = 0; t <= 400; ++t) {
    if (xAt(t) <= 1000 + MaskPoller::kPadPx - 1) {
      entered = t;
      break;
    }
  }
  ASSERT_GE(entered, 0);
  ASSERT_GE(flipped, 0);
  EXPECT_LE(flipped - entered, MaskPollerHub::kFastIntervalMs);
  EXPECT_LT(ticks, (flipped / MaskPollerHub::kFastIntervalMs) / 2);
}

TEST(MaskPollerTest, ARealTimerTickFlipsTheWindow) {
  Fixture f;
  f.hub = std::make_unique<MaskPollerHub>([&f] { return f.cursor; });  // the real clock
  f.poller = std::make_unique<MaskPoller>(&f.window, f.hub.get(), [&f] { return f.mask; });
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  EXPECT_TRUE(QTest::qWaitFor([&] { return f.clickThrough(); }, 2000));
  f.at(10, 10);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !f.clickThrough(); }, 2000));
}
