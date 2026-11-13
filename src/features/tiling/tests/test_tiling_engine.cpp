// The tiler's behaviour as event sequences against a fake placer and a manual clock. No test
// touches a real window.
#include <gtest/gtest.h>

#include <QVariantMap>

#include "features/tiling/layouts.h"
#include "features/tiling/testing/fake_window_placer.h"
#include "features/tiling/tiling_engine.h"

using namespace waku::tiling;
using waku::tiling::testing::FakeScheduler;
using waku::tiling::testing::FakeWindowPlacer;
using waku::tiling::testing::kMonitorA;
using waku::tiling::testing::kMonitorB;

namespace {

constexpr int kGap = 10;
const QRect kAreaA(0, 0, 1920, 1040);

struct Rig {
  Rig() {
    TilingSettings settings;
    settings.gap = kGap;
    settings.ratio = 0.5;
    engine.setSettings(settings);
  }

  void send(EventKind kind, WindowId hwnd) { engine.handle({kind, hwnd}); }
  void settle() { scheduler.runAll(); }
  // A window that shows after the engine runs.
  void open(WindowId hwnd, const QString& process = QStringLiteral("app.exe")) {
    placer.add(hwnd, process);
    send(EventKind::Show, hwnd);
    settle();
  }
  QRect slot(size_t index, size_t count) {
    return layoutSlots(QStringLiteral("mainStack"), kAreaA, static_cast<int>(count), kGap,
                       0.5)[index];
  }

  FakeWindowPlacer placer;
  FakeScheduler scheduler;
  TilingEngine engine{placer, scheduler};
};

}  // namespace

TEST(TilingEngine, AWindowThatAppearsIsPlacedInTheFullArea) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
  EXPECT_EQ(rig.engine.model().managedCount(), 1);
}

TEST(TilingEngine, TheEvaluationWaitsOneEventLoopTurn) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1);
  rig.send(EventKind::Show, 1);
  EXPECT_TRUE(rig.placer.placements.empty());
  EXPECT_GT(rig.scheduler.pending(), 0u);
  rig.settle();
  EXPECT_EQ(rig.placer.placements.size(), 1u);
}

TEST(TilingEngine, TheSecondWindowMakesMainAndStack) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 2));
  rig.open(3);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 3));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 3));
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(2, 3));
}

TEST(TilingEngine, WindowsThatShowInOneTurnArePlacedOnce) {
  Rig rig;
  rig.engine.start();
  for (WindowId id : {1, 2, 3}) {
    rig.placer.add(id);
    rig.send(EventKind::Show, id);
  }
  rig.settle();
  for (WindowId id : {1, 2, 3}) EXPECT_EQ(rig.placer.slotsOf(id).size(), 1u) << id;
}

TEST(TilingEngine, ExistingWindowsAreTiledAtStart) {
  Rig rig;
  rig.placer.add(1);
  rig.placer.add(2);
  rig.engine.start();
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 2));
}

TEST(TilingEngine, MaximizedWindowsAreRestoredAndTiledAtStart) {
  Rig rig;
  rig.placer.add(1).zoomed = true;
  rig.placer.windows[1].fullscreen = true;
  rig.placer.add(2);
  rig.engine.start();
  rig.settle();
  EXPECT_EQ(rig.placer.restored, (std::vector<WindowId>{1}));
  EXPECT_FALSE(rig.placer.windows[1].zoomed);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 2));
  EXPECT_TRUE(rig.placer.activated.empty());
}

TEST(TilingEngine, RestartReclaimsMaximizationButRetilesLeaveItAlone) {
  Rig rig;
  rig.placer.add(1);
  rig.engine.start();
  rig.settle();
  rig.placer.windows[1].zoomed = true;
  rig.engine.retileAll();
  rig.settle();
  EXPECT_TRUE(rig.placer.restored.empty());
  EXPECT_TRUE(rig.placer.windows[1].zoomed);

  rig.engine.stop();
  rig.engine.start();
  rig.settle();
  EXPECT_EQ(rig.placer.restored, (std::vector<WindowId>{1}));
  EXPECT_FALSE(rig.placer.windows[1].zoomed);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));

  const size_t placements = rig.placer.placements.size();
  rig.placer.windows[1].zoomed = true;
  rig.engine.retileAll();
  rig.settle();
  EXPECT_EQ(rig.placer.restored.size(), 1u);
  EXPECT_TRUE(rig.placer.windows[1].zoomed);
  EXPECT_EQ(rig.placer.placements.size(), placements);
}

TEST(TilingEngine, StartupRestoreWaitsForTheWindowToLeaveMaximizedState) {
  Rig rig;
  rig.placer.add(1).zoomed = true;
  rig.placer.delayedRestores.insert(1);
  rig.engine.start();
  rig.scheduler.advance(0);
  EXPECT_EQ(rig.placer.restored, (std::vector<WindowId>{1}));
  EXPECT_TRUE(rig.placer.placements.empty());
  rig.scheduler.advance(TilingEngine::kRetryMs);
  EXPECT_TRUE(rig.placer.placements.empty());

  rig.placer.windows[1].zoomed = false;
  rig.settle();
  EXPECT_EQ(rig.placer.restored.size(), 1u);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
}

TEST(TilingEngine, StartupRestoreOnAnotherDesktopWaitsUntilItIsShown) {
  Rig rig;
  auto& window = rig.placer.add(1);
  window.zoomed = true;
  window.cloaked = true;
  window.desktopId = QStringLiteral("{D2}");
  rig.placer.add(2);
  rig.engine.start();
  rig.settle();
  EXPECT_TRUE(rig.placer.restored.empty());
  EXPECT_TRUE(rig.placer.cloakedPlacements.empty());

  rig.placer.desktop = QStringLiteral("{D2}");
  rig.placer.windows[2].cloaked = true;
  window.cloaked = false;
  rig.send(EventKind::Uncloaked, 1);
  rig.settle();
  EXPECT_EQ(rig.placer.restored, (std::vector<WindowId>{1}));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
  EXPECT_TRUE(rig.placer.cloakedPlacements.empty());
}

TEST(TilingEngine, StartupRestorationHonorsRulesAndUnsafeStates) {
  Rig rig;
  TilingSettings settings = rig.engine.settings();
  settings.rules =
      parseRules({QVariantMap{{QStringLiteral("process"), QStringLiteral("ignore.exe")}},
                  QVariantMap{{QStringLiteral("process"), QStringLiteral("float.exe")},
                              {QStringLiteral("action"), QStringLiteral("float")}}})
          .rules;
  rig.placer.add(1).elevated = true;
  rig.placer.add(2, QStringLiteral("ignore.exe"));
  rig.placer.add(3, QStringLiteral("float.exe"));
  rig.placer.add(4).iconic = true;
  rig.placer.add(5).fullscreen = true;
  rig.placer.add(6).toolWindow = true;
  for (const WindowId hwnd : {1, 2, 3, 4, 6}) rig.placer.windows[hwnd].zoomed = true;
  rig.engine.start();
  rig.engine.setSettings(settings);
  rig.settle();
  EXPECT_TRUE(rig.placer.restored.empty());
  EXPECT_TRUE(rig.placer.placements.empty());
  EXPECT_TRUE(rig.engine.model().isFloating(3));
}

TEST(TilingEngine, FailedStartupRestoreDoesNotBlockOtherWindows) {
  Rig rig;
  rig.placer.add(1).zoomed = true;
  rig.placer.refuse.insert(1);
  rig.placer.add(2);
  rig.engine.start();
  rig.settle();
  EXPECT_EQ(rig.placer.restored, (std::vector<WindowId>{1}));
  EXPECT_FALSE(rig.engine.model().contains(1));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(0, 1));
}

TEST(TilingEngine, StartupRestorationStopsWaitingIfTheWindowNeverRestores) {
  Rig rig;
  rig.placer.add(1).zoomed = true;
  rig.placer.delayedRestores.insert(1);
  rig.engine.start();
  rig.settle();
  EXPECT_EQ(rig.placer.restored, (std::vector<WindowId>{1}));
  EXPECT_TRUE(rig.placer.placements.empty());
  EXPECT_EQ(rig.scheduler.pending(), 0u);
}

TEST(TilingEngine, ClosingAWindowRetilesTheOthers) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.windows.erase(2);
  rig.send(EventKind::Destroy, 2);
  rig.settle();
  EXPECT_EQ(rig.engine.model().managedCount(), 2);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(1, 2));
}

TEST(TilingEngine, AHiddenWindowLeavesTheLayout) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.placer.windows[2].visible = false;
  rig.send(EventKind::Hide, 2);
  rig.settle();
  EXPECT_FALSE(rig.engine.model().contains(2));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
}

TEST(TilingEngine, MinimizingAndRestoringMovesTheWindowOutAndBack) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.windows[1].iconic = true;
  rig.send(EventKind::MinimizeStart, 1);
  rig.settle();
  EXPECT_FALSE(rig.engine.model().contains(1));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(1, 2));

  rig.placer.windows[1].iconic = false;
  rig.send(EventKind::MinimizeEnd, 1);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().contains(1));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(0, 3));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(2, 3));  // back at the end of the stack
}

TEST(TilingEngine, AMinimizedWindowIsLookedAtAgainShortly) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1).iconic = true;
  rig.send(EventKind::Show, 1);
  rig.scheduler.advance(0);
  EXPECT_TRUE(rig.placer.placements.empty());
  rig.scheduler.advance(250);
  rig.placer.windows[1].iconic = false;
  rig.scheduler.advance(200);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
}

TEST(TilingEngine, AnUntitledWindowGetsItsTitleWithinTheRetries) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1).title.clear();
  rig.send(EventKind::Show, 1);
  rig.scheduler.advance(0);
  rig.scheduler.advance(120);
  EXPECT_TRUE(rig.placer.placements.empty());
  rig.placer.windows[1].title = QStringLiteral("ready");
  rig.scheduler.advance(120);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
}

TEST(TilingEngine, RetriesStopAfterFiveAndANameChangeStartsAgain) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1).title.clear();
  rig.send(EventKind::Show, 1);
  const int before = rig.placer.queries;
  rig.settle();
  // The first look and five retries.
  EXPECT_EQ(rig.placer.queries - before, 1 + TilingEngine::kMaxRetries);
  EXPECT_TRUE(rig.placer.placements.empty());
  EXPECT_EQ(rig.scheduler.pending(), 0u);

  rig.placer.windows[1].title = QStringLiteral("late title");
  rig.send(EventKind::NameChange, 1);
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
}

TEST(TilingEngine, ANameChangeOfAManagedOrRejectedWindowCostsNothing) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.placer.add(2).toolWindow = true;
  rig.send(EventKind::Show, 2);
  rig.settle();
  const int before = rig.placer.queries;
  rig.send(EventKind::NameChange, 1);
  rig.send(EventKind::NameChange, 2);
  rig.settle();
  EXPECT_EQ(rig.placer.queries, before);
}

TEST(TilingEngine, AShowOfAWindowRejectedForGoodCostsNothing) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(2).toolWindow = true;
  rig.send(EventKind::Show, 2);
  rig.settle();
  const int before = rig.placer.queries;
  rig.send(EventKind::Show, 2);
  rig.settle();
  EXPECT_EQ(rig.placer.queries, before);
}

TEST(TilingEngine, AShowOfAWindowThatWasMaximizedLooksAgain) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1).zoomed = true;
  rig.send(EventKind::Show, 1);
  rig.settle();
  EXPECT_FALSE(rig.engine.model().contains(1));
  rig.placer.windows[1].zoomed = false;
  rig.send(EventKind::Show, 1);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().contains(1));
}

TEST(TilingEngine, AHideOfAWindowTheTilerNeverHeldCostsNothing) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(5);
  const int before = rig.placer.queries;
  rig.send(EventKind::Hide, 5);
  rig.settle();
  EXPECT_EQ(rig.placer.queries, before);
}

TEST(TilingEngine, DraggingAWindowFloatsItAndTheOthersFillTheGap) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.send(EventKind::MoveSizeStart, 2);
  rig.placer.windows[2].rect.moveTopLeft(QPoint(400, 300));  // the user moved it
  rig.send(EventKind::LocationChange, 2);
  rig.send(EventKind::MoveSizeEnd, 2);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().isFloating(2));
  EXPECT_FALSE(rig.engine.model().contains(2));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(1, 2));
  // The floating window is not placed again, by a later retile either.
  const size_t placements = rig.placer.slotsOf(2).size();
  rig.engine.retileAll();
  rig.settle();
  EXPECT_EQ(rig.placer.slotsOf(2).size(), placements);
}

TEST(TilingEngine, AClickOnATitleBarWithoutAMoveKeepsTheWindowTiled) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.send(EventKind::MoveSizeStart, 2);
  rig.send(EventKind::MoveSizeEnd, 2);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().contains(2));
  EXPECT_FALSE(rig.engine.model().isFloating(2));
}

TEST(TilingEngine, MainStackDividerResizeUpdatesRatioAndNeighborsDuringDrag) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  const QRect oldMain = rig.placer.windows[1].rect;
  const QRect oldStack = rig.placer.lastSlot(2);
  rig.placer.onPlace = [&rig](WindowId hwnd, const QRect&) {
    rig.send(EventKind::LocationChange, hwnd);
  };

  rig.send(EventKind::MoveSizeStart, 1);
  ASSERT_TRUE(rig.engine.tracksMouseResize(1));
  rig.placer.windows[1].rect.setWidth(oldMain.width() + 120);
  rig.send(EventKind::LocationChange, 1);
  rig.settle();

  const WorkspaceKey key = *rig.engine.model().keyOf(1);
  ASSERT_TRUE(rig.engine.model().view(key).ratio.has_value());
  EXPECT_GT(*rig.engine.model().view(key).ratio, 0.5);
  EXPECT_LT(rig.placer.lastSlot(2).width(), oldStack.width());
  EXPECT_TRUE(rig.engine.model().contains(1));
  EXPECT_FALSE(rig.engine.model().isFloating(1));

  rig.send(EventKind::MoveSizeEnd, 1);
  rig.settle();
  EXPECT_FALSE(rig.engine.tracksMouseResize(1));
  EXPECT_TRUE(rig.engine.model().contains(1));
}

TEST(TilingEngine, MainStackResizeConsumesEndGeometryWithoutALocationEvent) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  const QRect original = rig.placer.windows[1].rect;
  const QRect expected(original.x(), original.y(), original.width() + 120, original.height());

  rig.send(EventKind::MoveSizeStart, 1);
  rig.placer.windows[1].rect = expected;
  rig.send(EventKind::MoveSizeEnd, 1);
  rig.settle();

  EXPECT_EQ(rig.placer.windows[1].rect, expected);
  EXPECT_EQ(rig.placer.windows[2].rect.x(), expected.right() + 1 + kGap);
  EXPECT_TRUE(rig.engine.model().contains(1));
  EXPECT_FALSE(rig.engine.model().isFloating(1));
  EXPECT_FALSE(rig.engine.tracksMouseResize(1));
}

TEST(TilingEngine, MainStackResizeConsumesEndGeometryNewerThanTheLastLocationEvent) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  const QRect original = rig.placer.windows[2].rect;
  const QRect expected(original.x() - 120, original.y(), original.width() + 120,
                       original.height() + 80);

  rig.send(EventKind::MoveSizeStart, 2);
  rig.placer.windows[2].rect =
      QRect(original.x() - 40, original.y(), original.width() + 40, original.height() + 30);
  rig.send(EventKind::LocationChange, 2);
  rig.settle();
  rig.placer.windows[2].rect = expected;
  rig.send(EventKind::MoveSizeEnd, 2);
  rig.settle();

  const QRect achieved = rig.placer.windows[2].rect;
  EXPECT_EQ(achieved.x(), expected.x());
  EXPECT_EQ(achieved.width(), expected.width());
  EXPECT_NEAR(achieved.height(), expected.height(), 1);
  EXPECT_EQ(rig.placer.windows[3].rect.y(), achieved.bottom() + 1 + kGap);
  EXPECT_EQ(rig.placer.windows[1].rect.right() + 1 + kGap, achieved.x());
  EXPECT_TRUE(rig.engine.model().contains(2));
  EXPECT_FALSE(rig.engine.model().isFloating(2));
  EXPECT_FALSE(rig.engine.tracksMouseResize(2));
}

TEST(TilingEngine, MainStackResizeUsesTheSharedEdgeAfterAHorizontalFlip) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.placer.front = 1;
  ASSERT_TRUE(rig.engine.flip(Axis::Horizontal).ok);
  rig.settle();
  const QRect oldStack = rig.placer.windows[2].rect;
  const QRect oldMain = rig.placer.lastSlot(1);
  const WorkspaceKey key = *rig.engine.model().keyOf(1);

  rig.send(EventKind::MoveSizeStart, 2);
  rig.placer.windows[2].rect.setWidth(oldStack.width() + 100);
  rig.send(EventKind::LocationChange, 2);
  rig.settle();

  ASSERT_TRUE(rig.engine.model().view(key).ratio.has_value());
  EXPECT_LT(*rig.engine.model().view(key).ratio, 0.5);
  EXPECT_LT(rig.placer.lastSlot(1).width(), oldMain.width());
  rig.send(EventKind::MoveSizeEnd, 2);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().contains(2));
  EXPECT_FALSE(rig.engine.model().isFloating(2));
}

TEST(TilingEngine, ResizingAnOuterEdgeSnapsBackInsideTheWorkArea) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  const QRect original = rig.placer.windows[1].rect;

  rig.send(EventKind::MoveSizeStart, 1);
  rig.placer.windows[1].rect.setX(original.x() + 40);
  rig.placer.windows[1].rect.setWidth(original.width() - 40);
  rig.send(EventKind::LocationChange, 1);
  rig.send(EventKind::MoveSizeEnd, 1);
  rig.settle();

  EXPECT_TRUE(rig.engine.model().contains(1));
  EXPECT_FALSE(rig.engine.model().isFloating(1));
  EXPECT_EQ(rig.placer.windows[1].rect, original);
}

TEST(TilingEngine, CancellingAMainStackResizeRestoresItsOriginalSizing) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  const QRect original = rig.placer.windows[1].rect;

  rig.send(EventKind::MoveSizeStart, 1);
  rig.placer.windows[1].rect.setWidth(original.width() + 100);
  rig.send(EventKind::LocationChange, 1);
  rig.settle();
  const WorkspaceKey key = *rig.engine.model().keyOf(1);
  ASSERT_TRUE(rig.engine.model().view(key).ratio.has_value());

  rig.placer.windows[1].rect = original;
  rig.send(EventKind::MoveSizeEnd, 1);
  rig.settle();
  EXPECT_FALSE(rig.engine.tracksMouseResize(1));
  EXPECT_FALSE(rig.engine.model().view(key).ratio.has_value());
  EXPECT_TRUE(rig.engine.model().contains(1));
}

TEST(TilingEngine, MainStackStackResizeChangesWeightsAndFollowsVerticalFlip) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.front = 1;
  ASSERT_TRUE(rig.engine.flip(Axis::Vertical).ok);
  rig.settle();
  const WorkspaceKey key = *rig.engine.model().keyOf(2);
  const QRect oldSecond = rig.placer.windows[2].rect;
  const QRect oldThird = rig.placer.lastSlot(3);

  rig.send(EventKind::MoveSizeStart, 2);
  rig.placer.windows[2].rect.setY(oldSecond.y() - 80);
  rig.placer.windows[2].rect.setHeight(oldSecond.height() + 80);
  rig.send(EventKind::LocationChange, 2);
  rig.settle();

  const auto& weights = rig.engine.model().view(key).weights;
  ASSERT_NE(weights.find(2), weights.end());
  EXPECT_GT(weights.at(2), 1.0);
  EXPECT_LT(rig.placer.lastSlot(3).height(), oldThird.height());
  rig.send(EventKind::MoveSizeEnd, 2);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().contains(2));
  EXPECT_FALSE(rig.engine.model().isFloating(2));
}

TEST(TilingEngine, ResizingAStackedSlotKeepsItsWindowsTogether) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.open(4);
  rig.placer.front = 4;
  ASSERT_TRUE(rig.engine.stackDirection(Direction::Up).ok);
  rig.settle();
  ASSERT_EQ(rig.engine.status().stacks, 1);
  ASSERT_TRUE(rig.engine.model().isTop(4));
  const std::vector<WindowId> group = rig.engine.model().stackOf(4);
  ASSERT_EQ(group.size(), 2u);
  const WindowId hidden = group.front() == 4 ? group.back() : group.front();
  const QRect oldSlot = rig.placer.lastSlot(4);

  rig.send(EventKind::MoveSizeStart, 4);
  ASSERT_TRUE(rig.engine.tracksMouseResize(4));
  rig.placer.windows[4].rect.setY(oldSlot.y() + 40);
  rig.placer.windows[4].rect.setHeight(oldSlot.height() - 40);
  rig.send(EventKind::LocationChange, 4);
  rig.settle();

  EXPECT_NE(rig.placer.lastSlot(hidden), oldSlot);
  EXPECT_TRUE(rig.engine.model().contains(hidden));
  rig.send(EventKind::MoveSizeEnd, 4);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().contains(4));
  EXPECT_EQ(rig.placer.windows[hidden].rect, rig.placer.windows[4].rect);
}

TEST(TilingEngine, ResizeInAnotherLayoutKeepsTheExistingFloatBehavior) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.placer.front = 2;
  ASSERT_TRUE(rig.engine.setWorkspaceLayout(QStringLiteral("columns")));
  rig.settle();

  rig.send(EventKind::MoveSizeStart, 2);
  EXPECT_FALSE(rig.engine.tracksMouseResize(2));
  rig.placer.windows[2].rect.setWidth(rig.placer.windows[2].rect.width() + 100);
  rig.send(EventKind::LocationChange, 2);
  rig.send(EventKind::MoveSizeEnd, 2);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().isFloating(2));
}

TEST(TilingEngine, ResizeTrackingStopsOnDestroyAndStop) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.send(EventKind::MoveSizeStart, 1);
  ASSERT_TRUE(rig.engine.tracksMouseResize(1));
  rig.placer.windows.erase(1);
  rig.send(EventKind::Destroy, 1);
  EXPECT_FALSE(rig.engine.tracksMouseResize(1));
  EXPECT_FALSE(rig.engine.model().contains(1));

  rig.send(EventKind::MoveSizeStart, 2);
  ASSERT_TRUE(rig.engine.tracksMouseResize(2));
  rig.engine.stop();
  EXPECT_FALSE(rig.engine.tracksMouseResize(2));
  rig.send(EventKind::LocationChange, 2);
  EXPECT_FALSE(rig.engine.model().contains(2));
}

TEST(TilingEngine, AWindowIsNotPlacedWhileTheUserHoldsIt) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.send(EventKind::MoveSizeStart, 2);
  const size_t before = rig.placer.slotsOf(2).size();
  rig.engine.retileAll();
  rig.settle();
  EXPECT_EQ(rig.placer.slotsOf(2).size(), before);
}

TEST(TilingEngine, ADesktopSwitchKeepsEachDesktopsStackAndNeverPlacesCloakedWindows) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  ASSERT_EQ(rig.engine.model().managedCount(), 2);

  // Switch to desktop D2: Windows cloaks the windows of D1 and the registry names D2.
  rig.placer.desktop = QStringLiteral("{D2}");
  for (WindowId id : {1, 2}) {
    rig.placer.windows[id].cloaked = true;
    rig.send(EventKind::Cloaked, id);
  }
  rig.settle();
  EXPECT_EQ(rig.engine.model().managedCount(), 2);  // kept, under their own desktop
  EXPECT_TRUE(rig.placer.cloakedPlacements.empty());

  rig.open(3);  // a window of D2
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(0, 1));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));  // D1's windows were not moved again

  // Back to D1.
  rig.placer.desktop = QStringLiteral("{D1}");
  rig.placer.windows[3].cloaked = true;
  rig.send(EventKind::Cloaked, 3);
  for (WindowId id : {1, 2}) {
    rig.placer.windows[id].cloaked = false;
    rig.send(EventKind::Uncloaked, id);
  }
  rig.placer.windows[1].rect = QRect(5, 5, 100, 100);  // something moved them meanwhile
  rig.placer.windows[2].rect = QRect(5, 5, 100, 100);
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 2));
  EXPECT_EQ(rig.engine.model().managedCount(), 3);
  EXPECT_TRUE(rig.placer.cloakedPlacements.empty());
}

TEST(TilingEngine, AWindowMovedToAnotherDesktopLeavesTheStackAndTheOthersRefill) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  ASSERT_EQ(rig.engine.model().managedCount(), 3);
  ASSERT_EQ(rig.placer.lastSlot(2), rig.slot(1, 3));

  // Window 3 is sent to D2 while D1 stays shown: Windows cloaks it, the shown desktop is unchanged.
  rig.placer.windows[3].desktopId = QStringLiteral("{D2}");
  rig.placer.windows[3].cloaked = true;
  rig.send(EventKind::Cloaked, 3);
  rig.settle();

  EXPECT_FALSE(rig.engine.model().contains(3));
  EXPECT_EQ(rig.engine.model().managedCount(), 2);
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 2));  // the freed slot is filled
}

TEST(TilingEngine, TheFirstUncloakAfterASwitchRetilesTheNewDesktop) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.placer.desktop = QStringLiteral("{D2}");
  rig.placer.windows[1].cloaked = true;
  rig.send(EventKind::Cloaked, 1);
  rig.settle();
  rig.placer.add(4);  // a D2 window that the tiler never saw
  rig.send(EventKind::Show, 4);
  rig.settle();
  rig.placer.desktop = QStringLiteral("{D1}");
  rig.placer.windows[1].cloaked = false;
  rig.placer.windows[1].rect = QRect(0, 0, 50, 50);
  rig.send(EventKind::Uncloaked, 1);
  rig.settle();
  EXPECT_EQ(rig.engine.status().desktop, QStringLiteral("{D1}"));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
}

TEST(TilingEngine, AWindowThatCloaksItselfLeavesTheLayout) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.placer.windows[2].cloaked = true;  // same desktop: the app hid itself
  rig.send(EventKind::Cloaked, 2);
  rig.settle();
  EXPECT_FALSE(rig.engine.model().contains(2));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
}

TEST(TilingEngine, WorkspacesOfTwoMonitorsAreIndependent) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1);
  rig.placer.add(2).monitor = kMonitorB;
  rig.send(EventKind::Show, 1);
  rig.send(EventKind::Show, 2);
  rig.settle();
  const QRect areaB = rig.placer.areas[kMonitorB];
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
  EXPECT_EQ(rig.placer.lastSlot(2),
            layoutSlots(QStringLiteral("mainStack"), areaB, 1, kGap, 0.5)[0]);
}

TEST(TilingEngine, AWorkAreaChangeRetilesOnRequest) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.placer.areas[kMonitorA] = QRect(0, 40, 1920, 1000);  // a bar appeared on top
  rig.engine.retileAll();
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1),
            layoutSlots(QStringLiteral("mainStack"), QRect(0, 40, 1920, 1000), 1, kGap, 0.5)[0]);
}

TEST(TilingEngine, AWindowThatClampsKeepsItsSlotAndLeavesAGap) {
  Rig rig;
  rig.placer.clamp = [](WindowId hwnd, const QRect& slot) {
    // Window 2 cannot get narrower than 2000 px, wider than its slot.
    return hwnd == 2 ? QRect(slot.x(), slot.y(), std::max(slot.width(), 2000), slot.height())
                     : slot;
  };
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.scheduler.advance(1000);  // the readback
  // No retry loop: window 2 was placed once, window 1 keeps its slot.
  EXPECT_EQ(rig.placer.slotsOf(2).size(), 1u);
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 2));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
  EXPECT_EQ(rig.engine.status().clamped, 1);
  EXPECT_EQ(rig.scheduler.pending(), 0u);
}

TEST(TilingEngine, AWindowThatRefusesIsLeftAlone) {
  Rig rig;
  rig.engine.start();
  rig.placer.refuse.insert(2);
  rig.open(1);
  rig.open(2);
  EXPECT_FALSE(rig.engine.model().contains(2));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));
  rig.send(EventKind::NameChange, 2);  // does not bring it back
  rig.settle();
  EXPECT_FALSE(rig.engine.model().contains(2));
}

TEST(TilingEngine, AnElevatedWindowIsUnmanagedUnlessWakuIsElevated) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1).elevated = true;
  rig.send(EventKind::Show, 1);
  rig.settle();
  EXPECT_TRUE(rig.placer.placements.empty());
  EXPECT_FALSE(rig.engine.model().contains(1));

  Rig elevated;
  TilingSettings settings = elevated.engine.settings();
  settings.selfElevated = true;
  elevated.engine.setSettings(settings);
  elevated.engine.start();
  elevated.placer.add(1).elevated = true;
  elevated.send(EventKind::Show, 1);
  elevated.settle();
  EXPECT_TRUE(elevated.engine.model().contains(1));
}

TEST(TilingEngine, WindowsThatAreNotTileableAreNeverPlaced) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1).ownProcess = true;
  rig.placer.add(2).className = QStringLiteral("MsoSplash");
  rig.placer.add(3).zoomed = true;
  rig.placer.add(4).ownedByVisible = true;
  rig.placer.add(5).thickFrame = false;
  for (WindowId id : {1, 2, 3, 4, 5}) rig.send(EventKind::Show, id);
  rig.settle();
  EXPECT_TRUE(rig.placer.placements.empty());
}

TEST(TilingEngine, AFloatRuleKeepsTheWindowOutUntilTheUserTilesIt) {
  Rig rig;
  TilingSettings settings = rig.engine.settings();
  settings.rules = parseRules({QVariantMap{{QStringLiteral("process"), QStringLiteral("calc.exe")},
                                           {QStringLiteral("action"), QStringLiteral("float")}}})
                       .rules;
  rig.engine.setSettings(settings);
  rig.engine.start();
  rig.open(1);
  rig.open(2, QStringLiteral("calc.exe"));
  EXPECT_TRUE(rig.engine.model().isFloating(2));
  EXPECT_TRUE(rig.placer.slotsOf(2).empty());
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 1));

  rig.placer.front = 2;
  EXPECT_EQ(rig.engine.setForegroundFloating(std::nullopt), FloatOutcome::Tiled);
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 2));
}

TEST(TilingEngine, AnIgnoreRuleLeavesTheWindowAlone) {
  Rig rig;
  TilingSettings settings = rig.engine.settings();
  settings.rules =
      parseRules({QVariantMap{{QStringLiteral("process"), QStringLiteral("bar.exe")}}}).rules;
  rig.engine.setSettings(settings);
  rig.engine.start();
  rig.open(1, QStringLiteral("bar.exe"));
  EXPECT_TRUE(rig.placer.placements.empty());
  EXPECT_FALSE(rig.engine.model().isFloating(1));
}

TEST(TilingEngine, ToggleFloatFloatsAndTilesTheForegroundWindow) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.front = 1;
  EXPECT_EQ(rig.engine.setForegroundFloating(std::nullopt), FloatOutcome::Floated);
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(0, 2));
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(1, 2));
  EXPECT_EQ(rig.engine.setForegroundFloating(true), FloatOutcome::Unchanged);
  EXPECT_EQ(rig.engine.setForegroundFloating(std::nullopt), FloatOutcome::Tiled);
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(2, 3));  // appended at the end
}

TEST(TilingEngine, ToggleFloatWithoutAForegroundWindowDoesNothing) {
  Rig rig;
  rig.engine.start();
  EXPECT_EQ(rig.engine.setForegroundFloating(std::nullopt), FloatOutcome::NoWindow);
}

TEST(TilingEngine, AMaximizedWindowTakesNoSlot) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.placer.windows[1].zoomed = true;  // the user maximized it
  rig.engine.retileAll();
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(0, 1));
  EXPECT_TRUE(rig.engine.model().contains(1));  // still managed: back in the layout when restored
  rig.placer.windows[1].zoomed = false;
  rig.engine.retileAll();
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 2));
}

TEST(TilingEngine, ALayoutNameAppliesToTheForegroundWorkspaceOnly) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.front = 1;
  EXPECT_TRUE(rig.engine.setWorkspaceLayout(QStringLiteral("columns")));
  EXPECT_FALSE(rig.engine.setWorkspaceLayout(QStringLiteral("spiral")));
  rig.settle();
  const auto columns = layoutSlots(QStringLiteral("columns"), kAreaA, 3, kGap, 0.5);
  EXPECT_EQ(rig.placer.lastSlot(1), columns[0]);
  EXPECT_EQ(rig.placer.lastSlot(3), columns[2]);
}

TEST(TilingEngine, CycleLayoutWalksTheListOfTheForegroundWorkspaceAndWraps) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.front = 1;
  const QStringList names = waku::tiling::layoutNames();
  EXPECT_EQ(rig.engine.currentLayout(), names.first());
  EXPECT_EQ(rig.engine.cycleLayout(1).message, names.at(1));
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1), layoutSlots(names.at(1), kAreaA, 3, kGap, 0.5)[0]);
  EXPECT_EQ(rig.engine.status().layout, names.at(1));
  EXPECT_EQ(rig.engine.cycleLayout(-1).message, names.first());
  EXPECT_EQ(rig.engine.cycleLayout(-1).message, names.last());  // wraps
  EXPECT_EQ(rig.engine.cycleLayout(1).message, names.first());
}

TEST(TilingEngine, ColumnsRowsAndGridDoNotResize) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.placer.front = 1;
  for (const char* layout : {"columns", "rows", "grid"}) {
    rig.engine.setWorkspaceLayout(QString::fromLatin1(layout));
    EXPECT_FALSE(rig.engine.resize(Axis::Horizontal, 1).ok) << layout;
  }
  rig.engine.setWorkspaceLayout(QStringLiteral("bsp"));
  EXPECT_TRUE(rig.engine.resize(Axis::Horizontal, 1).ok);
}

TEST(TilingEngine, FocusStepActivatesTheNextAndPreviousWindowWithWrap) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.front = 3;
  EXPECT_TRUE(rig.engine.focusStep(1));
  EXPECT_EQ(rig.placer.activated.back(), 1u);  // wrapped
  EXPECT_TRUE(rig.engine.focusStep(-1));
  EXPECT_EQ(rig.placer.activated.back(), 3u);
}

TEST(TilingEngine, TheTilerNeverActivatesAWindowWhilePlacing) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.engine.retileAll();
  rig.settle();
  EXPECT_TRUE(rig.placer.activated.empty());
}

TEST(TilingEngine, StopForgetsEverythingAndDropsPendingWork) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.placer.add(2);
  rig.send(EventKind::Show, 2);
  rig.engine.stop();
  const size_t placements = rig.placer.placements.size();
  rig.settle();
  EXPECT_EQ(rig.placer.placements.size(), placements);
  EXPECT_EQ(rig.engine.model().managedCount(), 0);
  EXPECT_FALSE(rig.engine.running());
  rig.send(EventKind::Show, 2);  // events after stop do nothing
  rig.settle();
  EXPECT_EQ(rig.placer.placements.size(), placements);
}

TEST(TilingEngine, ChangingTheGapRetilesTheWindows) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  TilingSettings settings = rig.engine.settings();
  settings.gap = 0;
  rig.engine.setSettings(settings);
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1), kAreaA);
}

// --- the focused-window commands

namespace {

// Three windows (1 main, 2 and 3 in the stack), 3 in front.
void openThree(Rig& rig) {
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.front = 3;
}

}  // namespace

TEST(TilingCommands, FocusDirectionActivatesTheGeometricNeighbour) {
  Rig rig;
  openThree(rig);
  EXPECT_TRUE(rig.engine.focusDirection(Direction::Up).ok);
  EXPECT_EQ(rig.placer.front, 2u);
  EXPECT_TRUE(rig.engine.focusDirection(Direction::Left).ok);
  EXPECT_EQ(rig.placer.front, 1u);
  EXPECT_TRUE(rig.engine.focusDirection(Direction::Right).ok);  // a stack window, level with it
  EXPECT_TRUE(rig.engine.focusDirection(Direction::Left).ok);   // and back to the main window
  EXPECT_EQ(rig.placer.front, 1u);
  EXPECT_FALSE(rig.engine.focusDirection(Direction::Left).ok);
}

TEST(TilingCommands, FocusDirectionNeedsATiledForegroundWindow) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.placer.front = 99;
  EXPECT_FALSE(rig.engine.focusDirection(Direction::Left).ok);
  EXPECT_TRUE(rig.placer.activated.empty());
}

TEST(TilingCommands, MoveSwapsWithTheNeighbourAndRetiles) {
  Rig rig;
  openThree(rig);
  EXPECT_TRUE(rig.engine.moveDirection(Direction::Left).ok);  // 3 swaps with the main window
  rig.settle();
  EXPECT_EQ(rig.engine.model().stack(*rig.engine.model().keyOf(3)),
            (std::vector<WindowId>{3, 2, 1}));
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(0, 3));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(2, 3));
  EXPECT_TRUE(rig.placer.activated.empty());
}

TEST(TilingCommands, PromoteGivesTheMainSlot) {
  Rig rig;
  openThree(rig);
  EXPECT_EQ(rig.engine.promote().message, QStringLiteral("promoted"));
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(0, 3));
  EXPECT_EQ(rig.engine.promote().message, QStringLiteral("already the main window"));
}

TEST(TilingCommands, ResizeMovesTheRatioPerWorkspaceAndClamps) {
  Rig rig;
  openThree(rig);
  rig.placer.front = 1;  // the main window
  const int before = rig.placer.lastSlot(1).width();
  EXPECT_EQ(rig.engine.resize(Axis::Horizontal, 1).message, QStringLiteral("resized"));
  rig.settle();
  EXPECT_GT(rig.placer.lastSlot(1).width(), before);
  rig.placer.front = 2;  // a stack window: increase takes width from the main window
  const int main = rig.placer.lastSlot(1).width();
  rig.engine.resize(Axis::Horizontal, 1);
  rig.settle();
  EXPECT_LT(rig.placer.lastSlot(1).width(), main);
  for (int i = 0; i < 40; ++i) rig.engine.resize(Axis::Horizontal, 1);
  rig.settle();
  EXPECT_EQ(rig.engine.resize(Axis::Horizontal, 1).message, QStringLiteral("unchanged"));
  // The ratio is clamped to 0.1: the main window keeps a tenth of the width.
  EXPECT_LE(rig.placer.lastSlot(1).width(), kAreaA.width() * 2 / 10);
}

TEST(TilingCommands, VerticalResizeChangesTheHeightOfAStackedWindow) {
  Rig rig;
  openThree(rig);
  rig.placer.front = 2;
  const int before = rig.placer.lastSlot(2).height();
  EXPECT_EQ(rig.engine.resize(Axis::Vertical, 1).message, QStringLiteral("resized"));
  rig.settle();
  EXPECT_GT(rig.placer.lastSlot(2).height(), before);
  EXPECT_LT(rig.placer.lastSlot(3).height(), before);
  rig.placer.front = 1;
  EXPECT_EQ(rig.engine.resize(Axis::Vertical, 1).message, QStringLiteral("unchanged"));
}

TEST(TilingCommands, FlipMirrorsTheLayoutAndAgainUndoesIt) {
  Rig rig;
  openThree(rig);
  const QRect plain = rig.placer.lastSlot(1);
  EXPECT_EQ(rig.engine.flip(Axis::Horizontal).message, QStringLiteral("flip horizontal on"));
  rig.settle();
  EXPECT_GT(rig.placer.lastSlot(1).left(), plain.left());
  EXPECT_EQ(rig.engine.flip(Axis::Horizontal).message, QStringLiteral("flip horizontal off"));
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(1), plain);
}

TEST(TilingCommands, MonocleFillsTheAreaAndLeavesTheOthersUntouched) {
  Rig rig;
  openThree(rig);
  const size_t placementsBefore = rig.placer.placements.size();
  EXPECT_EQ(rig.engine.monocle().message, QStringLiteral("monocle on"));
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(0, 1));
  for (size_t i = placementsBefore; i < rig.placer.placements.size(); ++i) {
    EXPECT_EQ(rig.placer.placements[i].hwnd, 3u);
  }
  EXPECT_EQ(rig.engine.monocle().message, QStringLiteral("monocle off"));
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(3), rig.slot(2, 3));
  EXPECT_EQ(rig.placer.lastSlot(1), rig.slot(0, 3));
}

TEST(TilingCommands, PausePlacesNothingUntilResumed) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  EXPECT_EQ(rig.engine.togglePause().message, QStringLiteral("paused"));
  EXPECT_TRUE(rig.engine.status().paused);
  const size_t placed = rig.placer.placements.size();
  rig.open(2);
  rig.engine.retileAll();
  rig.settle();
  EXPECT_EQ(rig.placer.placements.size(), placed);
  EXPECT_EQ(rig.engine.togglePause().message, QStringLiteral("resumed"));
  rig.settle();
  EXPECT_EQ(rig.placer.lastSlot(2), rig.slot(1, 2));
}

TEST(TilingCommands, CloseAndMinimizeActOnTheForegroundWindow) {
  Rig rig;
  rig.engine.start();
  rig.open(1);
  rig.placer.front = 1;
  EXPECT_TRUE(rig.engine.closeForeground().ok);
  EXPECT_TRUE(rig.engine.minimizeForeground().ok);
  EXPECT_EQ(rig.placer.closed, std::vector<WindowId>{1});
  EXPECT_EQ(rig.placer.minimized, std::vector<WindowId>{1});
}

TEST(TilingCommands, CloseRefusesOwnWindowsTheDesktopAndNoWindow) {
  Rig rig;
  rig.engine.start();
  rig.placer.add(1).ownProcess = true;
  rig.placer.add(2).className = QStringLiteral("Progman");
  rig.placer.front = 1;
  EXPECT_FALSE(rig.engine.closeForeground().ok);
  rig.placer.front = 2;
  EXPECT_FALSE(rig.engine.closeForeground().ok);
  rig.placer.front = std::nullopt;
  EXPECT_FALSE(rig.engine.minimizeForeground().ok);
  EXPECT_TRUE(rig.placer.closed.empty());
  EXPECT_TRUE(rig.placer.minimized.empty());
}

// --- stacking

namespace {

// Three tiled windows (1 main, 2 and 3 in the column), 3 focused.
void openStackRig(Rig& rig) {
  rig.engine.start();
  rig.open(1);
  rig.open(2);
  rig.open(3);
  rig.placer.front = 3;
}

}  // namespace

TEST(TilingStacking, StackLeftPutsTheWindowOnTheMainSlot) {
  Rig rig;
  openStackRig(rig);
  EXPECT_TRUE(rig.engine.stackDirection(Direction::Left).ok);
  rig.settle();
  // Two slots now: main [1 3] and the column [2]. Everything shows at its slot.
  EXPECT_EQ(rig.placer.windows[3].rect, rig.slot(0, 2));
  EXPECT_EQ(rig.placer.windows[1].rect, rig.slot(0, 2));
  EXPECT_EQ(rig.placer.windows[2].rect, rig.slot(1, 2));
  EXPECT_EQ(rig.engine.status().stacks, 1);
  EXPECT_EQ(rig.engine.status().managed, 3);
  ASSERT_FALSE(rig.placer.raised.empty());
  EXPECT_EQ(rig.placer.raised.back(), 3u);  // the moved window is on top
}

TEST(TilingStacking, ThereIsNoStackWhereThereIsNoNeighbour) {
  Rig rig;
  openStackRig(rig);
  EXPECT_FALSE(rig.engine.stackDirection(Direction::Right).ok);
  EXPECT_FALSE(rig.engine.unstack().ok);
  EXPECT_FALSE(rig.engine.cycleStack(1).ok);
}

TEST(TilingStacking, CycleBringsTheOtherWindowToTheTopAndFocusesIt) {
  Rig rig;
  openStackRig(rig);
  rig.engine.stackDirection(Direction::Left);
  rig.settle();
  rig.placer.raised.clear();
  EXPECT_TRUE(rig.engine.cycleStack(1).ok);
  rig.settle();
  EXPECT_EQ(rig.placer.front, std::optional<WindowId>(1));
  EXPECT_EQ(rig.placer.raised, std::vector<WindowId>{1});
  EXPECT_TRUE(rig.engine.model().isTop(1));
  EXPECT_TRUE(rig.engine.cycleStack(-1).ok);
  EXPECT_EQ(rig.placer.front, std::optional<WindowId>(3));
}

TEST(TilingStacking, UnstackGivesTheWindowItsOwnSlotAfterTheStack) {
  Rig rig;
  openStackRig(rig);
  rig.engine.stackDirection(Direction::Left);
  rig.settle();
  EXPECT_TRUE(rig.engine.unstack().ok);
  rig.settle();
  EXPECT_EQ(rig.engine.status().stacks, 0);
  // Order: 1, 3, 2 - three slots again.
  EXPECT_EQ(rig.placer.windows[1].rect, rig.slot(0, 3));
  EXPECT_EQ(rig.placer.windows[3].rect, rig.slot(1, 3));
  EXPECT_EQ(rig.placer.windows[2].rect, rig.slot(2, 3));
}

TEST(TilingStacking, ClosingTheTopShowsTheNextWindow) {
  Rig rig;
  openStackRig(rig);
  rig.engine.stackDirection(Direction::Left);
  rig.settle();
  rig.placer.raised.clear();
  rig.placer.windows.erase(3);
  rig.send(EventKind::Destroy, 3);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().isTop(1));
  EXPECT_EQ(rig.engine.status().stacks, 0);
  EXPECT_EQ(rig.placer.windows[1].rect, rig.slot(0, 2));
}

TEST(TilingStacking, AHiddenWindowThatTakesTheForegroundBecomesTheTop) {
  Rig rig;
  openStackRig(rig);
  rig.engine.stackDirection(Direction::Left);
  rig.settle();
  rig.placer.front = 1;
  rig.send(EventKind::Foreground, 1);
  rig.settle();
  EXPECT_TRUE(rig.engine.model().isTop(1));
  EXPECT_EQ(rig.placer.raised.back(), 1u);
}

TEST(TilingStacking, FloatingAStackedWindowLeavesTheStackAlone) {
  Rig rig;
  openStackRig(rig);
  rig.engine.stackDirection(Direction::Left);
  rig.settle();
  EXPECT_EQ(rig.engine.setForegroundFloating(true), FloatOutcome::Floated);
  rig.settle();
  EXPECT_EQ(rig.engine.status().stacks, 0);
  EXPECT_EQ(rig.engine.status().managed, 2);
  EXPECT_EQ(rig.placer.windows[1].rect, rig.slot(0, 2));
}

TEST(TilingStacking, FocusAndMoveTreatAStackAsOneSlot) {
  Rig rig;
  openStackRig(rig);
  rig.engine.stackDirection(Direction::Left);  // slots: [1 3] [2]
  rig.settle();
  EXPECT_TRUE(rig.engine.focusDirection(Direction::Right).ok);
  EXPECT_EQ(rig.placer.front, std::optional<WindowId>(2));
  EXPECT_TRUE(rig.engine.focusDirection(Direction::Left).ok);
  EXPECT_EQ(rig.placer.front, std::optional<WindowId>(3));  // the top of the stack
  EXPECT_TRUE(rig.engine.moveDirection(Direction::Right).ok);
  rig.settle();
  EXPECT_EQ(rig.placer.windows[3].rect, rig.slot(1, 2));  // the whole stack moved
  EXPECT_EQ(rig.placer.windows[1].rect, rig.slot(1, 2));
  EXPECT_EQ(rig.placer.windows[2].rect, rig.slot(0, 2));
}
