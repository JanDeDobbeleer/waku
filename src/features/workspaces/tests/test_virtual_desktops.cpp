#include <gtest/gtest.h>

#include <QSignalSpy>
#include <memory>

#include "features/workspaces/qml/virtual_desktops_service.h"
#include "features/workspaces/qml/workspaces_service.h"
#include "features/workspaces/virtual_desktops.h"

using namespace waku::workspaces;

namespace {

// A system whose state and recorded switches the test controls.
class FakeSystem final : public vdesk::VirtualDesktopSystem {
 public:
  std::optional<vdesk::State> read() override { return state; }
  bool jumpTo(int index) override {
    jumps.push_back(index);
    return jumpWorks;
  }
  void create() override { ++created; }

  std::optional<vdesk::State> state;
  std::vector<int> jumps;
  int created = 0;
  bool jumpWorks = true;
};

vdesk::State threeDesktops(int current) {
  vdesk::State state;
  state.desktops = {{QStringLiteral("{A}"), QStringLiteral("Mail"), 2},
                    {QStringLiteral("{B}"), QString(), 0},
                    {QStringLiteral("{C}"), QString(), 1}};
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
    full.desktops.push_back({QStringLiteral("{%1}").arg(i), QString(), 0});
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
  Workspaces workspaces(nullptr, nullptr, h.desktops.get());
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

TEST(WorkspacesWindowsProvider, FocusAndStepSwitchDesktopsOnAnyMonitor) {
  Harness h;
  Workspaces workspaces(nullptr, nullptr, h.desktops.get());
  workspaces.focus(12345, QStringLiteral("2"));
  EXPECT_EQ(h.system->jumps, (std::vector<int>{2}));
  h.system->state = threeDesktops(2);
  h.desktops->refresh();
  workspaces.focusPrevious(12345);
  EXPECT_EQ(h.system->jumps, (std::vector<int>({2, 1})));
}

TEST(WorkspacesWindowsProvider, FollowsTheDesktops) {
  Harness h;
  Workspaces workspaces(nullptr, nullptr, h.desktops.get());
  QSignalSpy spy(&workspaces, &Workspaces::changed);
  h.system->state = threeDesktops(0);
  h.desktops->refresh();
  EXPECT_EQ(spy.count(), 1);
  h.system->state.reset();
  h.desktops->refresh();
  EXPECT_EQ(workspaces.provider(), QString());
  EXPECT_FALSE(workspaces.connected());
}
