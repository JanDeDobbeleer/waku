#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>
#include <optional>
#include <set>

#include "platform/systray_host.h"
#include "widgets/systray/systray_state.h"
#include "widgets/systray/tray_icon_registry.h"

using namespace yasb::widgets;
using yasb::platform::TrayIconData;
namespace platform = yasb::platform;

namespace {

class LiveWindows final : public platform::TrayWindowApi {
 public:
  bool isWindow(std::uint64_t hwnd) override { return alive.count(hwnd) != 0; }
  void allowSetForeground(std::uint64_t) override {}
  void sendNotifyMessage(std::uint64_t, std::uint32_t, std::uint64_t, std::int64_t) override {}
  std::pair<int, int> cursorPos() override { return {0, 0}; }
  void broadcastTaskbarCreated() override {}
  std::set<std::uint64_t> alive;
};

const QUuid kGuidA(QStringLiteral("{11111111-1111-1111-1111-111111111111}"));
const QUuid kGuidB(QStringLiteral("{22222222-2222-2222-2222-222222222222}"));

constexpr std::uint32_t kDefaultFlags =
    platform::kNifMessage | platform::kNifIcon | platform::kNifTip;

TrayIconData icon(std::uint64_t hwnd, std::uint32_t uid, const QString& exe = QStringLiteral("app"),
                  std::optional<QUuid> guid = std::nullopt, std::uint32_t flags = kDefaultFlags,
                  const QString& tip = QStringLiteral("tip")) {
  TrayIconData data;
  data.hWnd = hwnd;
  data.uID = uid;
  data.exe = exe;
  data.exePath = QStringLiteral("C:/apps/") + exe + QStringLiteral(".exe");
  data.uFlags = flags | (guid ? platform::kNifGuid : 0u);
  data.guid = guid;
  data.szTip = tip;
  data.uCallbackMessage = 0x8001;
  data.hIcon = 1000 + uid;
  return data;
}

}  // namespace

// ---- arrival, identity and merging ----

TEST(TrayIconRegistry, NewIconsGetIncreasingIdsInArrivalOrder) {
  TrayIconRegistry registry;
  const auto a = registry.modified(icon(1, 1));
  const auto b = registry.modified(icon(2, 1));
  EXPECT_EQ(a.change, TrayIconRegistry::Change::Added);
  EXPECT_EQ(b.change, TrayIconRegistry::Change::Added);
  EXPECT_LT(a.id, b.id);
  EXPECT_EQ(registry.size(), 2u);
  EXPECT_EQ(registry.ids(), (std::vector<int>{a.id, b.id}));
  ASSERT_NE(registry.data(a.id), nullptr);
  EXPECT_EQ(registry.data(a.id)->hWnd, 1u);
  EXPECT_EQ(registry.data(b.id)->hWnd, 2u);
}

TEST(TrayIconRegistry, ANotificationForTheSameWindowAndIdUpdatesTheIcon) {
  TrayIconRegistry registry;
  const auto added = registry.modified(
      icon(1, 1, QStringLiteral("app"), std::nullopt, kDefaultFlags, QStringLiteral("one")));
  const auto updated = registry.modified(
      icon(1, 1, QStringLiteral("app"), std::nullopt, kDefaultFlags, QStringLiteral("two")));
  EXPECT_EQ(updated.change, TrayIconRegistry::Change::Updated);
  EXPECT_EQ(updated.id, added.id);
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_EQ(registry.data(added.id)->szTip, QStringLiteral("two"));
}

TEST(TrayIconRegistry, ADifferentIconIdOfTheSameWindowIsAnotherIcon) {
  TrayIconRegistry registry;
  const auto first = registry.modified(icon(1, 1));
  const auto second = registry.modified(icon(1, 2));
  EXPECT_EQ(second.change, TrayIconRegistry::Change::Added);
  EXPECT_NE(first.id, second.id);
}

TEST(TrayIconRegistry, OnlyTheFieldsNamedByTheFlagsAreCopiedIn) {
  TrayIconRegistry registry;
  const auto added = registry.modified(
      icon(1, 1, QStringLiteral("app"), std::nullopt, kDefaultFlags, QStringLiteral("old")));
  // The second notification names only the icon: the tip it carries is not applied.
  registry.modified(
      icon(1, 1, QStringLiteral("app"), std::nullopt, platform::kNifIcon, QStringLiteral("new")));
  const TrayIconData* data = registry.data(added.id);
  EXPECT_EQ(data->szTip, QStringLiteral("old"));
  EXPECT_EQ(data->uCallbackMessage, 0x8001u);
}

TEST(TrayIconRegistry, AGuidFindsTheIconEvenWhenItsWindowAndIdChange) {
  TrayIconRegistry registry;
  const auto added = registry.modified(icon(1, 1, QStringLiteral("app"), kGuidA));
  const auto moved = registry.modified(icon(2, 9, QStringLiteral("app"), kGuidA));
  EXPECT_EQ(moved.change, TrayIconRegistry::Change::Updated);
  EXPECT_EQ(moved.id, added.id);
  EXPECT_EQ(registry.size(), 1u);
  EXPECT_EQ(registry.data(added.id)->hWnd, 2u);
  EXPECT_EQ(registry.data(added.id)->uID, 9u);
}

TEST(TrayIconRegistry, TheGuidWinsOverAMatchingWindowAndId) {
  TrayIconRegistry registry;
  const auto x = registry.modified(icon(1, 1, QStringLiteral("x"), kGuidA));
  const auto y = registry.modified(icon(2, 2, QStringLiteral("y"), kGuidB));
  // Window and id say X, the guid says Y: the guid decides.
  const auto result = registry.modified(icon(1, 1, QStringLiteral("y"), kGuidB));
  EXPECT_EQ(result.id, y.id);
  EXPECT_NE(result.id, x.id);
}

TEST(TrayIconRegistry, WithoutAGuidTheWindowAndIdStillMatchAnIconThatHasOne) {
  TrayIconRegistry registry;
  const auto added = registry.modified(icon(1, 1, QStringLiteral("app"), kGuidA));
  const auto again = registry.modified(icon(1, 1, QStringLiteral("app")));
  EXPECT_EQ(again.change, TrayIconRegistry::Change::Updated);
  EXPECT_EQ(again.id, added.id);
}

TEST(TrayIconRegistry, FindReportsKnownAndUnknownIcons) {
  TrayIconRegistry registry;
  const auto added = registry.modified(icon(1, 1, QStringLiteral("app"), kGuidA));
  EXPECT_EQ(registry.find(kGuidA, 99, 99), added.id);
  EXPECT_EQ(registry.find(std::nullopt, 1, 1), added.id);
  EXPECT_FALSE(registry.find(kGuidB, 99, 99).has_value());
  EXPECT_FALSE(registry.find(std::nullopt, 5, 5).has_value());
  EXPECT_EQ(registry.data(12345), nullptr);
}

TEST(TrayIconRegistry, UpdatingAnIconDoesNotReorderThem) {
  TrayIconRegistry registry;
  const auto a = registry.modified(icon(1, 1));
  const auto b = registry.modified(icon(2, 1));
  registry.modified(
      icon(1, 1, QStringLiteral("app"), std::nullopt, kDefaultFlags, QStringLiteral("changed")));
  EXPECT_EQ(registry.ids(), (std::vector<int>{a.id, b.id}));
}

// ---- the filter ----

TEST(TrayIconRegistry, FilteredGuidsAreNeverAdmitted) {
  TrayIconRegistry registry;
  registry.setFilter({kGuidA}, {});
  const auto blocked = registry.modified(icon(1, 1, QStringLiteral("app"), kGuidA));
  EXPECT_EQ(blocked.change, TrayIconRegistry::Change::Ignored);
  EXPECT_EQ(blocked.id, -1);
  EXPECT_EQ(registry.size(), 0u);
  EXPECT_EQ(registry.modified(icon(2, 1, QStringLiteral("app"), kGuidB)).change,
            TrayIconRegistry::Change::Added);
}

TEST(TrayIconRegistry, HiddenExecutablesAreMatchedWithoutRegardToCase) {
  TrayIconRegistry registry;
  registry.setFilter({}, {QStringLiteral("STEAM"), QStringLiteral("Discord")});
  EXPECT_EQ(registry.modified(icon(1, 1, QStringLiteral("steam"))).change,
            TrayIconRegistry::Change::Ignored);
  EXPECT_EQ(registry.modified(icon(2, 1, QStringLiteral("DISCORD"))).change,
            TrayIconRegistry::Change::Ignored);
  EXPECT_EQ(registry.modified(icon(3, 1, QStringLiteral("other"))).change,
            TrayIconRegistry::Change::Added);
  EXPECT_TRUE(registry.isFiltered(icon(9, 9, QStringLiteral("Steam"))));
  EXPECT_FALSE(registry.isFiltered(icon(9, 9, QStringLiteral("steamer"))));  // the whole name
}

TEST(TrayIconRegistry, WithoutAFilterNothingIsHidden) {
  TrayIconRegistry registry;
  EXPECT_FALSE(registry.isFiltered(icon(1, 1, QStringLiteral("steam"), kGuidA)));
}

TEST(TrayIconRegistry, ALaterFilterStopsFurtherUpdatesOfAKnownIcon) {
  TrayIconRegistry registry;
  const auto added = registry.modified(
      icon(1, 1, QStringLiteral("app"), std::nullopt, kDefaultFlags, QStringLiteral("before")));
  registry.setFilter({}, {QStringLiteral("app")});
  const auto blocked = registry.modified(
      icon(1, 1, QStringLiteral("app"), std::nullopt, kDefaultFlags, QStringLiteral("after")));
  EXPECT_EQ(blocked.change, TrayIconRegistry::Change::Ignored);
  EXPECT_EQ(registry.data(added.id)->szTip, QStringLiteral("before"));
}

// ---- deletion ----

TEST(TrayIconRegistry, DeletingRemovesTheIconAndReturnsItsId) {
  TrayIconRegistry registry;
  const auto a = registry.modified(icon(1, 1));
  const auto b = registry.modified(icon(2, 1));
  const auto removed = registry.deleted(icon(1, 1));
  ASSERT_TRUE(removed.has_value());
  EXPECT_EQ(*removed, a.id);
  EXPECT_EQ(registry.ids(), (std::vector<int>{b.id}));
  EXPECT_EQ(registry.data(a.id), nullptr);
}

TEST(TrayIconRegistry, DeletingByGuidWorksToo) {
  TrayIconRegistry registry;
  const auto a = registry.modified(icon(1, 1, QStringLiteral("app"), kGuidA));
  TrayIconData deletion;  // a delete notification carries the guid, window and id only
  deletion.guid = kGuidA;
  EXPECT_EQ(registry.deleted(deletion), a.id);
  EXPECT_EQ(registry.size(), 0u);
}

TEST(TrayIconRegistry, DeletingAnUnknownIconIsNotAnError) {
  TrayIconRegistry registry;
  registry.modified(icon(1, 1));
  EXPECT_FALSE(registry.deleted(icon(7, 7)).has_value());
  EXPECT_EQ(registry.size(), 1u);
}

TEST(TrayIconRegistry, TheFilterDoesNotApplyToDeletion) {
  TrayIconRegistry registry;
  const auto added = registry.modified(icon(1, 1, QStringLiteral("app")));
  registry.setFilter({}, {QStringLiteral("app")});
  EXPECT_EQ(registry.deleted(icon(1, 1, QStringLiteral("app"))), added.id);
}

TEST(TrayIconRegistry, IdsAreNeverReusedAfterADeletion) {
  TrayIconRegistry registry;
  const auto first = registry.modified(icon(1, 1));
  registry.deleted(icon(1, 1));
  const auto again = registry.modified(icon(1, 1));  // the same program announces itself again
  EXPECT_EQ(again.change, TrayIconRegistry::Change::Added);
  EXPECT_NE(again.id, first.id);
}

// ---- the stale sweep ----

TEST(TrayIconRegistry, SweepRemovesIconsWhoseWindowIsGoneAndKeepsTheOthers) {
  TrayIconRegistry registry;
  const auto a = registry.modified(icon(1, 1));
  const auto b = registry.modified(icon(2, 1));
  const auto c = registry.modified(icon(3, 1));
  LiveWindows api;
  api.alive = {1, 3};
  EXPECT_EQ(registry.sweepStale(api), (std::vector<int>{b.id}));
  EXPECT_EQ(registry.ids(), (std::vector<int>{a.id, c.id}));
  EXPECT_TRUE(registry.sweepStale(api).empty());  // nothing more to remove
}

TEST(TrayIconRegistry, SweepReportsEveryRemovedIconInArrivalOrder) {
  TrayIconRegistry registry;
  const auto a = registry.modified(icon(1, 1));
  const auto b = registry.modified(icon(2, 1));
  LiveWindows api;
  EXPECT_EQ(registry.sweepStale(api), (std::vector<int>{a.id, b.id}));
  EXPECT_EQ(registry.size(), 0u);
}

TEST(TrayIconRegistry, SweepOfAnEmptyRegistryIsEmpty) {
  TrayIconRegistry registry;
  LiveWindows api;
  EXPECT_TRUE(registry.sweepStale(api).empty());
}

// ---- the saved state of an icon ----

TEST(SystraySavedState, AnIconIsLookedUpByItsKeyThenByItsExecutablePath) {
  IconStateMap state;
  const TrayIconData withGuid = icon(1, 1, QStringLiteral("app"), kGuidA);
  state.set(iconStateKey(withGuid), IconState{true, 3});
  EXPECT_EQ(savedStateFor(state, withGuid), (IconState{true, 3}));

  // No entry under the guid: the executable path is tried next.
  IconStateMap byPath;
  byPath.set(withGuid.exePath, IconState{true, 5});
  EXPECT_EQ(savedStateFor(byPath, withGuid), (IconState{true, 5}));
}

TEST(SystraySavedState, TheKeyBeatsTheExecutablePath) {
  IconStateMap state;
  const TrayIconData data = icon(1, 1, QStringLiteral("app"), kGuidA);
  state.set(data.exePath, IconState{true, 5});
  state.set(iconStateKey(data), IconState{false, 1});
  EXPECT_EQ(savedStateFor(state, data), (IconState{false, 1}));
}

TEST(SystraySavedState, AnIconWithoutAGuidUsesItsExecutablePathAsTheKey) {
  IconStateMap state;
  const TrayIconData data = icon(1, 1, QStringLiteral("app"));
  state.set(data.exePath, IconState{true, 2});
  EXPECT_EQ(savedStateFor(state, data), (IconState{true, 2}));
}

TEST(SystraySavedState, NothingSavedMeansUnpinnedWithoutAPosition) {
  const IconStateMap state;
  EXPECT_EQ(savedStateFor(state, icon(1, 1)), (IconState{false, -1}));
}
