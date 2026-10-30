// MaskPoller with a fake cursor and a real (offscreen) window: when the click-through flag flips.
#include <gtest/gtest.h>

#include <QGuiApplication>
#include <QTest>
#include <QWindow>
#include <optional>

#include "host/mask_poller.h"

using namespace wabar::host;

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
  // The cursor moves: still fast 299 ms later, idle after 300 ms without movement.
  f.at(300, 200);
  f.now = 1000;
  f.hub->tick();
  f.now = 1299;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kFastIntervalMs);
  f.now = 1301;
  f.hub->tick();
  EXPECT_EQ(f.hub->intervalMs(), MaskPollerHub::kIdleIntervalMs);
  // Movement brings the fast rate back at once.
  f.at(310, 200);
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

TEST(MaskPollerTest, ARealTimerTickFlipsTheWindow) {
  Fixture f;
  f.hub = std::make_unique<MaskPollerHub>([&f] { return f.cursor; });  // the real clock
  f.poller = std::make_unique<MaskPoller>(&f.window, f.hub.get(), [&f] { return f.mask; });
  f.window.show();
  f.poller->refresh();
  f.at(300, 200);
  QTest::qWait(80);
  EXPECT_TRUE(f.clickThrough());
  f.at(10, 10);
  QTest::qWait(80);
  EXPECT_FALSE(f.clickThrough());
}
