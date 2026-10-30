// MaskPoller with a fake cursor and a real (offscreen) window: when the click-through flag flips.
#include <gtest/gtest.h>

#include <QGuiApplication>
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
  std::unique_ptr<MaskPoller> poller;

  Fixture() {
    window.setGeometry(200, 100, 400, 300);
    cursor = QPoint(0, 0);
    mask = rect(0, 0, 100, 50);
    poller =
        std::make_unique<MaskPoller>(&window, [this] { return cursor; }, [this] { return mask; });
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

TEST(MaskPollerTest, ThePollTimerRunsAtAbout120Hz) {
  EXPECT_EQ(MaskPoller::kIntervalMs, 8);
  EXPECT_EQ(MaskPoller::kPadPx, 6);
}
