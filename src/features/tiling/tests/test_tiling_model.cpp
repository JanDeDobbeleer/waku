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
