#include <gtest/gtest.h>

#include <QSignalSpy>
#include <QTest>
#include <functional>
#include <memory>

#include "features/workspaces/qml/virtual_desktops_service.h"
#include "features/workspaces/qml/workspaces_service.h"
#include "features/workspaces/virtual_desktops.h"

using namespace waku::workspaces;

namespace {

// A system whose state and recorded switches the test controls.
class FakeSystem final : public vdesk::VirtualDesktopSystem {
 public:
  std::optional<vdesk::State> read() override {
    ++reads;
    return state;
  }
  bool jumpTo(int index) override {
    jumps.push_back(index);
    return jumpWorks;
  }
  int create() override {
    ++created;
    if (state && addOnCreate) state->desktops.push_back({QStringLiteral("{N}"), QString(), {}});
    return createdIndex;
  }
  MoveResult moveForegroundWindow(int index) override {
    moves.push_back(index);
    return moveResult;
  }
  bool canRemove() const override { return supportsRemove; }
  bool remove(int index) override {
    removed.push_back(index);
    return removeWorks;
  }
  bool rename(int index, const QString& name) override {
    renamed.push_back({index, name});
    return true;
  }
  void setAnimate(bool animate) override { animateValue = animate; }
  bool setChangedHook(std::function<void()> h) override {
    hook = std::move(h);
    return hasHook;
  }

  std::optional<vdesk::State> state;
  std::vector<int> jumps;
  int created = 0;
  int createdIndex = -1;  // -1: the system showed the desktop itself
  bool addOnCreate = true;
  std::vector<int> moves;
  MoveResult moveResult = MoveResult::Moved;
  bool jumpWorks = true;
  bool supportsRemove = true;
  bool removeWorks = true;
  bool hasHook = true;
  bool animateValue = true;
  std::vector<int> removed;
  std::vector<std::pair<int, QString>> renamed;
  std::function<void()> hook;
  int reads = 0;
};

vdesk::State threeDesktops(int current) {
  vdesk::State state;
  state.desktops = {{QStringLiteral("{A}"), QStringLiteral("Mail"), {101, 102}},
                    {QStringLiteral("{B}"), QString(), {}},
                    {QStringLiteral("{C}"), QString(), {201}}};
  state.current = current;
  return state;
}

struct Harness {
  FakeSystem* system = nullptr;
  std::unique_ptr<VirtualDesktops> desktops;

  Harness() {
    auto fake = std::make_unique<FakeSystem>();
    system = fake.get();
    system->state = threeDesktops(1);
    desktops = std::make_unique<VirtualDesktops>(std::move(fake), nullptr);
  }
};

}  // namespace

TEST(VirtualDesktopIds, ParsesRegistryBytes) {
  // {E1138AA6-0C10-442E-8F32-86BEB2B5963F} as the registry stores it, then a partial entry.
  const QByteArray bytes = QByteArray::fromHex("a68a13e1100c2e448f3286beb2b5963f0102");
  EXPECT_EQ(vdesk::parseDesktopIds(bytes),
            QStringList{QStringLiteral("{E1138AA6-0C10-442E-8F32-86BEB2B5963F}")});
  EXPECT_TRUE(vdesk::parseDesktopIds(QByteArray()).isEmpty());
}

TEST(VirtualDesktops, IsUnavailableWithoutASystem) {
  VirtualDesktops desktops(nullptr, nullptr);
  EXPECT_FALSE(desktops.available());
  EXPECT_TRUE(desktops.desktops().isEmpty());
  desktops.switchTo(1);  // no crash
}

TEST(VirtualDesktops, ListsDesktopsWithLabels) {
  Harness h;
  ASSERT_TRUE(h.desktops->available());
  EXPECT_EQ(h.desktops->current(), 1);
  ASSERT_EQ(h.desktops->desktops().size(), 3);
  const QVariantMap first = h.desktops->desktops().at(0).toMap();
  EXPECT_EQ(first.value(QStringLiteral("label")).toString(), QStringLiteral("Mail"));
  EXPECT_EQ(first.value(QStringLiteral("windowCount")).toInt(), 2);
  EXPECT_EQ(first.value(QStringLiteral("windows")).toList(),
            (QVariantList{qlonglong(101), qlonglong(102)}));
  EXPECT_TRUE(
      h.desktops->desktops().at(1).toMap().value(QStringLiteral("windows")).toList().isEmpty());
  const QVariantMap second = h.desktops->desktops().at(1).toMap();
  EXPECT_EQ(second.value(QStringLiteral("label")).toString(), QStringLiteral("2"));
  EXPECT_TRUE(second.value(QStringLiteral("current")).toBool());
}

TEST(VirtualDesktops, EmitsChangedOnlyWhenTheListDiffers) {
  Harness h;
  QSignalSpy spy(h.desktops.get(), &VirtualDesktops::changed);
  EXPECT_FALSE(h.desktops->refresh());
  EXPECT_EQ(spy.count(), 0);
  h.system->state = threeDesktops(2);
  EXPECT_TRUE(h.desktops->refresh());
  EXPECT_EQ(spy.count(), 1);
  h.system->state.reset();
  EXPECT_TRUE(h.desktops->refresh());
  EXPECT_FALSE(h.desktops->available());
  EXPECT_EQ(h.desktops->current(), -1);
}

TEST(VirtualDesktops, AMovedWindowEmitsChangedOnce) {
  Harness h;
  QSignalSpy spy(h.desktops.get(), &VirtualDesktops::changed);
  vdesk::State moved = threeDesktops(1);
  moved.desktops[0].windows = {101};  // 102 moves to the empty desktop
  moved.desktops[1].windows = {102};
  h.system->state = moved;
  EXPECT_TRUE(h.desktops->refresh());
  EXPECT_EQ(spy.count(), 1);
  EXPECT_EQ(h.desktops->current(), 1);
  EXPECT_EQ(h.desktops->desktops().at(0).toMap().value(QStringLiteral("windowCount")).toInt(), 1);
  EXPECT_EQ(h.desktops->desktops().at(1).toMap().value(QStringLiteral("windowCount")).toInt(), 1);
  EXPECT_FALSE(h.desktops->refresh());  // the same state again: nothing
  EXPECT_EQ(spy.count(), 1);
}

TEST(VirtualDesktops, AnUnchangedWindowListDoesNotEmit) {
  Harness h;
  QSignalSpy spy(h.desktops.get(), &VirtualDesktops::changed);
  h.system->state = threeDesktops(1);  // equal lists, built again
  EXPECT_FALSE(h.desktops->refresh());
  EXPECT_EQ(spy.count(), 0);
}

TEST(VirtualDesktops, JumpsStraightToTheDesktop) {
  Harness h;  // the second of three shown
  h.desktops->switchTo(2);
  h.desktops->switchTo(0);  // any distance is one jump
  h.desktops->switchTo(1);  // already shown
  h.desktops->switchTo(-1);
  EXPECT_EQ(h.system->jumps, (std::vector<int>{2, 0}));
  EXPECT_EQ(h.system->created, 0);
}

TEST(VirtualDesktops, DoesNothingWhenTheJumpIsRefused) {
  Harness h;
  h.system->jumpWorks = false;
  h.desktops->switchTo(2);
  EXPECT_EQ(h.system->jumps, (std::vector<int>{2}));
  EXPECT_EQ(h.system->created, 0);
}

TEST(VirtualDesktops, CreatesOnlyTheNextDesktop) {
  Harness h;                // three desktops
  h.desktops->switchTo(4);  // two past the end: ignored
  EXPECT_EQ(h.system->created, 0);
  h.desktops->switchTo(3);
  EXPECT_EQ(h.system->created, 1);
  EXPECT_TRUE(h.system->jumps.empty());
}

TEST(VirtualDesktops, StopsCreatingAtTheLimit) {
  Harness h;
  vdesk::State full;
  for (int i = 0; i < VirtualDesktops::kMaxDesktops; ++i) {
    full.desktops.push_back({QStringLiteral("{%1}").arg(i), QString(), {}});
  }
  full.current = 0;
  h.system->state = full;
  h.desktops->refresh();
  h.desktops->switchTo(VirtualDesktops::kMaxDesktops);
  EXPECT_EQ(h.system->created, 0);
}

TEST(VirtualDesktops, NextAndPreviousWrap) {
  Harness h;
  h.system->state = threeDesktops(2);
  h.desktops->refresh();
  h.desktops->next();  // wraps to the first
  h.system->state = threeDesktops(0);
  h.desktops->refresh();
  h.desktops->previous();  // wraps to the last
  EXPECT_EQ(h.system->jumps, (std::vector<int>({0, 2})));
}

TEST(WorkspacesWindowsProvider, ShowsDesktopsWhenNoManagerRuns) {
  Harness h;
  Workspaces workspaces(h.desktops.get());
  EXPECT_EQ(workspaces.provider(), QStringLiteral("windows"));
  ASSERT_EQ(workspaces.monitors().size(), 1);
  const QVariantMap monitor = workspaces.monitors().at(0).toMap();
  EXPECT_EQ(monitor.value(QStringLiteral("handle")).toLongLong(), 0);
  const QVariantList list = monitor.value(QStringLiteral("workspaces")).toList();
  ASSERT_EQ(list.size(), 3);
  const QVariantMap shown = list.at(1).toMap();
  EXPECT_TRUE(shown.value(QStringLiteral("focused")).toBool());
  EXPECT_TRUE(shown.value(QStringLiteral("active")).toBool());
  EXPECT_FALSE(shown.value(QStringLiteral("hasWindows")).toBool());
  EXPECT_TRUE(list.at(0).toMap().value(QStringLiteral("hasWindows")).toBool());
}

TEST(WorkspacesWindowsProvider, PassesTheWindowsThrough) {
  Harness h;
  Workspaces workspaces(h.desktops.get());
  const QVariantList list =
      workspaces.monitors().at(0).toMap().value(QStringLiteral("workspaces")).toList();
  ASSERT_EQ(list.size(), 3);
  const QVariantMap first = list.at(0).toMap();
  EXPECT_EQ(first.value(QStringLiteral("windows")).toList(),
            (QVariantList{qlonglong(101), qlonglong(102)}));
  EXPECT_EQ(first.value(QStringLiteral("windowCount")).toInt(), 2);
  EXPECT_TRUE(first.value(QStringLiteral("hasWindows")).toBool());
  const QVariantMap empty = list.at(1).toMap();
  EXPECT_TRUE(empty.value(QStringLiteral("windows")).toList().isEmpty());
  EXPECT_FALSE(empty.value(QStringLiteral("hasWindows")).toBool());
  EXPECT_EQ(list.at(2).toMap().value(QStringLiteral("windows")).toList(),
            (QVariantList{qlonglong(201)}));
}

TEST(WorkspacesWindowsProvider, FocusAndStepSwitchDesktopsOnAnyMonitor) {
  Harness h;
  Workspaces workspaces(h.desktops.get());
  workspaces.focus(12345, QStringLiteral("2"));
  EXPECT_EQ(h.system->jumps, (std::vector<int>{2}));
  h.system->state = threeDesktops(2);
  h.desktops->refresh();
  workspaces.focusPrevious(12345);
  EXPECT_EQ(h.system->jumps, (std::vector<int>({2, 1})));
}

TEST(WorkspacesWindowsProvider, FollowsTheDesktops) {
  Harness h;
  Workspaces workspaces(h.desktops.get());
  QSignalSpy spy(&workspaces, &Workspaces::changed);
  h.system->state = threeDesktops(0);
  h.desktops->refresh();
  EXPECT_EQ(spy.count(), 1);
  h.system->state.reset();
  h.desktops->refresh();
  EXPECT_EQ(workspaces.provider(), QString());
  EXPECT_FALSE(workspaces.connected());
}

TEST(RemovalFallback, FollowsTaskViewsRule) {
  EXPECT_EQ(vdesk::removalFallbackIndex(3, 0), 1);  // the first gives its windows to the right
  EXPECT_EQ(vdesk::removalFallbackIndex(3, 1), 0);  // the others to the left neighbour
  EXPECT_EQ(vdesk::removalFallbackIndex(3, 2), 1);
  EXPECT_EQ(vdesk::removalFallbackIndex(2, 0), 1);
  EXPECT_EQ(vdesk::removalFallbackIndex(2, 1), 0);
  EXPECT_EQ(vdesk::removalFallbackIndex(1, 0), -1);  // the last desktop is never closed
  EXPECT_EQ(vdesk::removalFallbackIndex(0, 0), -1);
  EXPECT_EQ(vdesk::removalFallbackIndex(3, 3), -1);
  EXPECT_EQ(vdesk::removalFallbackIndex(3, -1), -1);
}

TEST(VirtualDesktopsRemove, ForwardsTheIndexAndRefreshes) {
  Harness h;
  QSignalSpy spy(h.desktops.get(), &VirtualDesktops::changed);
  h.desktops->remove(1);
  EXPECT_EQ(h.system->removed, (std::vector<int>{1}));
  vdesk::State after = threeDesktops(0);
  after.desktops.erase(after.desktops.begin() + 1);
  h.system->state = after;
  ASSERT_TRUE(QTest::qWaitFor([&] { return spy.count() > 0; }, 2000));
  EXPECT_EQ(h.desktops->desktops().size(), 2);
}

TEST(VirtualDesktopsRemove, RefusesABadIndex) {
  Harness h;
  h.desktops->remove(-1);
  h.desktops->remove(3);
  h.desktops->remove(99);
  EXPECT_TRUE(h.system->removed.empty());
}

TEST(VirtualDesktopsRemove, RefusesTheLastDesktop) {
  Harness h;
  vdesk::State one;
  one.desktops = {{QStringLiteral("{A}"), QString(), {}}};
  one.current = 0;
  h.system->state = one;
  h.desktops->refresh();
  EXPECT_FALSE(h.desktops->canRemove());
  h.desktops->remove(0);
  EXPECT_TRUE(h.system->removed.empty());
}

TEST(VirtualDesktopsRemove, CanRemoveFollowsSupportAndCount) {
  Harness h;
  EXPECT_TRUE(h.desktops->canRemove());
  h.system->supportsRemove = false;
  h.desktops->refresh();
  EXPECT_FALSE(h.desktops->canRemove());
  h.desktops->remove(1);  // no route without the shell interface
  EXPECT_TRUE(h.system->removed.empty());
  h.system->supportsRemove = true;
  EXPECT_TRUE(h.desktops->refresh());  // canRemove is part of the model
  EXPECT_TRUE(h.desktops->canRemove());
}

TEST(VirtualDesktopsRemove, ARefusedRemoveChangesNothing) {
  Harness h;
  h.system->removeWorks = false;
  const int reads = h.system->reads;
  h.desktops->remove(0);
  EXPECT_EQ(h.system->removed, (std::vector<int>{0}));
  QTest::qWait(250);
  EXPECT_EQ(h.system->reads, reads);  // no refresh was scheduled
}

TEST(VirtualDesktopsRename, ForwardsAndIgnoresBadIndexes) {
  Harness h;
  h.desktops->rename(1, QStringLiteral("Work"));
  h.desktops->rename(7, QStringLiteral("x"));
  ASSERT_EQ(h.system->renamed.size(), 1u);
  EXPECT_EQ(h.system->renamed[0].first, 1);
  EXPECT_EQ(h.system->renamed[0].second, QStringLiteral("Work"));
}

TEST(VirtualDesktopsCreate, ShowsADesktopTheSystemMadeWithoutShowingIt) {
  Harness h;
  h.system->createdIndex = 3;
  h.desktops->switchTo(3);
  EXPECT_EQ(h.system->created, 1);
  EXPECT_EQ(h.system->jumps, (std::vector<int>{3}));
}

TEST(VirtualDesktopsCreate, DoesNotJumpWhenWindowsShowedIt) {
  Harness h;  // createdIndex -1: the keystroke route shows the new desktop itself
  h.desktops->switchTo(3);
  EXPECT_EQ(h.system->created, 1);
  EXPECT_TRUE(h.system->jumps.empty());
}

TEST(VirtualDesktopsHook, CoalescesBurstsIntoOneRead) {
  Harness h;
  ASSERT_TRUE(static_cast<bool>(h.system->hook));
  const int reads = h.system->reads;
  for (int i = 0; i < 5; ++i) h.system->hook();
  EXPECT_EQ(h.system->reads, reads);  // nothing at once
  ASSERT_TRUE(QTest::qWaitFor([&] { return h.system->reads > reads; }, 2000));
  QTest::qWait(150);
  EXPECT_EQ(h.system->reads, reads + 1);
}

TEST(VirtualDesktopsHook, ANotificationRefreshesTheModel) {
  Harness h;
  QSignalSpy spy(h.desktops.get(), &VirtualDesktops::changed);
  h.system->state = threeDesktops(2);  // the user switched in Task View
  h.system->hook();
  ASSERT_TRUE(QTest::qWaitFor([&] { return spy.count() > 0; }, 2000));
  EXPECT_EQ(h.desktops->current(), 2);
}

TEST(VirtualDesktopsAnimate, ReachesTheSystem) {
  Harness h;
  EXPECT_TRUE(h.desktops->animate());
  h.desktops->setAnimate(false);
  EXPECT_FALSE(h.system->animateValue);
  EXPECT_FALSE(h.desktops->animate());
}

TEST(WorkspacesWindowsProvider, RemoveClosesTheDesktopOnAnyMonitor) {
  Harness h;
  Workspaces workspaces(h.desktops.get());
  EXPECT_TRUE(workspaces.canRemove());
  workspaces.remove(12345, QStringLiteral("2"));
  workspaces.remove(0, QStringLiteral("not a number"));
  EXPECT_EQ(h.system->removed, (std::vector<int>{2}));
}

TEST(WorkspacesWindowsProvider, CanRemoveFollowsTheDesktops) {
  Harness h;
  Workspaces workspaces(h.desktops.get());
  QSignalSpy spy(&workspaces, &Workspaces::changed);
  h.system->supportsRemove = false;
  h.desktops->refresh();
  EXPECT_FALSE(workspaces.canRemove());
  EXPECT_EQ(spy.count(), 1);
}

TEST(VirtualDesktopsMove, APastTheLastNumberMakesOneNewDesktopAndMovesTheWindowThere) {
  Harness h;  // three desktops
  const QString answer = h.desktops->controlMove(7);
  EXPECT_EQ(h.system->created, 1);
  EXPECT_EQ(h.system->moves, (std::vector<int>{3}));
  EXPECT_EQ(answer, QStringLiteral("ok: moved to desktop 4"));
}

TEST(VirtualDesktopsMove, ANumberInRangeCreatesNothing) {
  Harness h;
  h.desktops->controlMove(1);
  EXPECT_EQ(h.system->created, 0);
  EXPECT_EQ(h.system->moves, (std::vector<int>{0}));
}

TEST(VirtualDesktopsMove, NothingIsMovedWhenTheDesktopWasNotCreated) {
  Harness h;
  h.system->addOnCreate = false;
  const QString answer = h.desktops->controlMove(4);
  EXPECT_TRUE(answer.startsWith(QStringLiteral("error:")));
  EXPECT_TRUE(h.system->moves.empty());
}
