#include <gtest/gtest.h>

#include "features/tiling/tiling_model.h"

using waku::tiling::TilingModel;
using waku::tiling::WindowId;
using waku::tiling::WorkspaceKey;

namespace {
const WorkspaceKey kA{1, QStringLiteral("{D1}")};
const WorkspaceKey kB{1, QStringLiteral("{D2}")};
const WorkspaceKey kC{2, QStringLiteral("{D1}")};
}  // namespace

TEST(TilingModel, AddAppendsAndKeepsOrder) {
  TilingModel model;
  model.add(kA, 10);
  model.add(kA, 11);
  model.add(kA, 12);
  EXPECT_EQ(model.stack(kA), (std::vector<WindowId>{10, 11, 12}));
  EXPECT_EQ(model.managedCount(), 3);
}

TEST(TilingModel, WorkspacesAreKeyedByMonitorAndDesktop) {
  TilingModel model;
  model.add(kA, 1);
  model.add(kB, 2);
  model.add(kC, 3);
  EXPECT_EQ(model.keys().size(), 3u);
  EXPECT_EQ(model.stack(kA), std::vector<WindowId>{1});
  EXPECT_EQ(model.stack(kB), std::vector<WindowId>{2});
  EXPECT_EQ(*model.keyOf(3), kC);
}

TEST(TilingModel, AddMovesAWindowBetweenWorkspaces) {
  TilingModel model;
  model.add(kA, 1);
  model.add(kA, 2);
  model.add(kB, 1);
  EXPECT_EQ(model.stack(kA), std::vector<WindowId>{2});
  EXPECT_EQ(*model.keyOf(1), kB);
}

TEST(TilingModel, RemoveReportsTheWorkspaceAndDropsEmptyOnes) {
  TilingModel model;
  model.add(kA, 1);
  const auto key = model.remove(1);
  ASSERT_TRUE(key.has_value());
  EXPECT_EQ(*key, kA);
  EXPECT_TRUE(model.keys().empty());
  EXPECT_FALSE(model.remove(1).has_value());
}

TEST(TilingModel, FloatingWindowsLeaveTheStackAndReturnOnAdd) {
  TilingModel model;
  model.add(kA, 1);
  model.setFloating(1, true);
  EXPECT_FALSE(model.contains(1));
  EXPECT_TRUE(model.isFloating(1));
  model.add(kA, 1);
  EXPECT_TRUE(model.contains(1));
  EXPECT_FALSE(model.isFloating(1));
}

TEST(TilingModel, LayoutsOverrideTheFallbackPerWorkspace) {
  TilingModel model;
  EXPECT_EQ(model.layout(kA, QStringLiteral("mainStack")), QStringLiteral("mainStack"));
  model.setLayout(kA, QStringLiteral("columns"));
  EXPECT_EQ(model.layout(kA, QStringLiteral("mainStack")), QStringLiteral("columns"));
  EXPECT_EQ(model.layout(kB, QStringLiteral("mainStack")), QStringLiteral("mainStack"));
  model.clearLayouts();
  EXPECT_EQ(model.layout(kA, QStringLiteral("mainStack")), QStringLiteral("mainStack"));
}

TEST(TilingModel, ForgetClearsStackAndFloat) {
  TilingModel model;
  model.add(kA, 1);
  model.setFloating(2, true);
  model.forget(1);
  model.forget(2);
  EXPECT_EQ(model.managedCount(), 0);
  EXPECT_EQ(model.floatingCount(), 0);
}

TEST(TilingModel, StackingShareASlotAndTheTopShows) {
  TilingModel model;
  for (WindowId id : {1, 2, 3}) model.add(kA, id);
  EXPECT_TRUE(model.stackOnto(3, 2));
  EXPECT_EQ(model.stack(kA), (std::vector<WindowId>{1, 3}));  // slot tops
  EXPECT_EQ(model.members(kA), (std::vector<WindowId>{1, 2, 3}));
  EXPECT_EQ(model.stackOf(2), (std::vector<WindowId>{2, 3}));
  EXPECT_EQ(model.stackCount(), 1);
  EXPECT_TRUE(model.isTop(3));
  EXPECT_FALSE(model.isTop(2));
  EXPECT_EQ(model.managedCount(), 3);
  EXPECT_FALSE(model.stackOnto(3, 2));  // already share a slot
  EXPECT_FALSE(model.stackOnto(1, 1));
}

TEST(TilingModel, TheStackOfOneDissolves) {
  TilingModel model;
  for (WindowId id : {1, 2, 3}) model.add(kA, id);
  model.stackOnto(3, 2);
  model.remove(3);
  EXPECT_EQ(model.stack(kA), (std::vector<WindowId>{1, 2}));
  EXPECT_EQ(model.stackCount(), 0);
}

TEST(TilingModel, RemovingTheTopShowsTheNextWindow) {
  TilingModel model;
  for (WindowId id : {1, 2, 3, 4}) model.add(kA, id);
  model.stackOnto(2, 1);
  model.stackOnto(3, 1);  // slot: 1 2 3, top 3
  model.raiseInStack(2);
  model.remove(2);  // the top goes: the next one down the list (3) shows
  EXPECT_EQ(model.stack(kA), (std::vector<WindowId>{3, 4}));
  EXPECT_EQ(model.stackOf(1), (std::vector<WindowId>{1, 3}));
  model.remove(1);  // not the top: 3 stays
  EXPECT_EQ(model.stack(kA), (std::vector<WindowId>{3, 4}));
  EXPECT_EQ(model.stackCount(), 0);
}

TEST(TilingModel, UnstackPutsTheWindowRightAfterTheStack) {
  TilingModel model;
  for (WindowId id : {1, 2, 3}) model.add(kA, id);
  model.stackOnto(2, 1);
  EXPECT_TRUE(model.unstack(2));
  EXPECT_EQ(model.stack(kA), (std::vector<WindowId>{1, 2, 3}));
  EXPECT_FALSE(model.unstack(2));  // alone in its slot
  EXPECT_FALSE(model.unstack(99));
}

TEST(TilingModel, CycleWrapsAndMovesTheTop) {
  TilingModel model;
  for (WindowId id : {1, 2, 3}) model.add(kA, id);
  model.stackOnto(2, 1);
  model.stackOnto(3, 1);  // 1 2 3, top 3
  EXPECT_EQ(model.cycle(3, 1), 1u);
  EXPECT_EQ(model.cycle(1, -1), 3u);
  EXPECT_EQ(model.cycle(3, -1), 2u);
  EXPECT_EQ(model.stack(kA), std::vector<WindowId>{2});
  model.add(kA, 9);
  EXPECT_EQ(model.cycle(9, 1), 0u);  // alone
}

TEST(TilingModel, SwapAndPromoteMoveWholeStacks) {
  TilingModel model;
  for (WindowId id : {1, 2, 3, 4}) model.add(kA, id);
  model.stackOnto(4, 3);  // slots: [1] [2] [3 4]
  EXPECT_TRUE(model.promote(3));
  EXPECT_EQ(model.stack(kA), (std::vector<WindowId>{4, 1, 2}));
  EXPECT_TRUE(model.swap(2, 4));
  EXPECT_EQ(model.stack(kA), (std::vector<WindowId>{2, 1, 4}));
  EXPECT_EQ(model.stackOf(4), (std::vector<WindowId>{3, 4}));
  EXPECT_FALSE(model.swap(3, 4));  // one slot
}

TEST(TilingModel, TheHeightWeightAndMonocleFollowTheTop) {
  TilingModel model;
  for (WindowId id : {1, 2}) model.add(kA, id);
  model.edit(kA).weights[1] = 2.0;
  model.edit(kA).monocle = 1;
  model.stackOnto(2, 1);
  EXPECT_EQ(model.view(kA).monocle, 2u);
  EXPECT_DOUBLE_EQ(model.view(kA).weights.at(2), 2.0);
  EXPECT_TRUE(model.raiseInStack(1));
  EXPECT_EQ(model.view(kA).monocle, 1u);
}
