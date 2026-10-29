#include <gtest/gtest.h>

#include "spike/tests/wm_fixtures.h"

// ActiveLayout (komorebi): the service half, on the fake backend and command runner. The widget
// itself is not instantiated: creating it needs the Komorebi singleton, which starts the real
// komorebi client (see test_builtin_widgets.cpp); qmlcachegen type-checks the QML in the build.

namespace {

kmb::JsonPtr stateWith(const std::string& layout, bool tile = true, bool paused = false,
                       bool monocle = false) {
  json state = *komorebiState();
  json& ws = state["monitors"]["elements"][0]["workspaces"]["elements"][1];
  ws["tile"] = tile;
  ws["layout"] = {{"Default", layout}};
  if (monocle) ws["monocle_container"] = {{"windows", {{"elements", json::array()}}}};
  state["is_paused"] = paused;
  return std::make_shared<const json>(std::move(state));
}

QString layoutOfFirst(const KomorebiHarness& h) {
  return h.komorebi->monitors().at(0).toMap().value(QStringLiteral("layout")).toString();
}

}  // namespace

TEST(ActiveLayoutService, MonitorsCarryTheLayoutOfTheShownWorkspace) {
  KomorebiHarness h;
  h.publish(QStringLiteral("connect"), stateWith("VerticalStack"));
  const QVariantMap monitor = h.komorebi->monitors().at(0).toMap();
  EXPECT_EQ(monitor.value(QStringLiteral("layout")).toString(), QStringLiteral("vertical_stack"));
  EXPECT_EQ(monitor.value(QStringLiteral("workspace")).toInt(), 1);
}

TEST(ActiveLayoutService, MapsEveryLayoutName) {
  KomorebiHarness h;
  const std::vector<std::pair<std::string, QString>> names = {
      {"BSP", "bsp"},
      {"Columns", "columns"},
      {"Rows", "rows"},
      {"Grid", "grid"},
      {"Scrolling", "scrolling"},
      {"VerticalStack", "vertical_stack"},
      {"HorizontalStack", "horizontal_stack"},
      {"UltrawideVerticalStack", "ultrawide_vertical_stack"},
      {"RightMainVerticalStack", "right_main_vertical_stack"}};
  for (const auto& [komorebi, key] : names) {
    h.publish(QStringLiteral("update"), stateWith(komorebi));
    EXPECT_EQ(layoutOfFirst(h), key) << komorebi;
  }
}

TEST(ActiveLayoutService, ReportsBlockingStatesBeforeTheLayout) {
  KomorebiHarness h;
  h.publish(QStringLiteral("update"), stateWith("BSP", true, true));
  EXPECT_EQ(layoutOfFirst(h), QStringLiteral("paused"));
  h.publish(QStringLiteral("update"), stateWith("BSP", false));
  EXPECT_EQ(layoutOfFirst(h), QStringLiteral("floating"));
  h.publish(QStringLiteral("update"), stateWith("BSP", true, false, true));
  EXPECT_EQ(layoutOfFirst(h), QStringLiteral("monocle"));
}

TEST(ActiveLayoutService, SetLayoutRunsWorkspaceLayout) {
  KomorebiHarness h;
  h.publish(QStringLiteral("connect"), stateWith("BSP"));
  h.komorebi->setLayout(0, QStringLiteral("vertical_stack"));
  ASSERT_EQ(h.runs.size(), 1u);
  EXPECT_EQ(h.runs[0][0].join(' '), QStringLiteral("workspace-layout 0 1 vertical-stack"));
}

TEST(ActiveLayoutService, CycleStepsAndWraps) {
  KomorebiHarness h;
  const QStringList layouts{QStringLiteral("bsp"), QStringLiteral("columns"),
                            QStringLiteral("grid")};
  h.publish(QStringLiteral("connect"), stateWith("BSP"));
  h.komorebi->cycleLayout(0, layouts, 1);
  h.komorebi->cycleLayout(0, layouts, -1);  // bsp is still shown: wraps to the end
  ASSERT_EQ(h.runs.size(), 2u);
  EXPECT_EQ(h.runs[0][0].join(' '), QStringLiteral("workspace-layout 0 1 columns"));
  EXPECT_EQ(h.runs[1][0].join(' '), QStringLiteral("workspace-layout 0 1 grid"));
}

TEST(ActiveLayoutService, CycleOutsideTheListStartsAtTheEnds) {
  KomorebiHarness h;
  h.publish(QStringLiteral("connect"), stateWith("Rows"));
  h.komorebi->cycleLayout(0, {QStringLiteral("bsp"), QStringLiteral("grid")}, 1);
  ASSERT_EQ(h.runs.size(), 1u);
  EXPECT_EQ(h.runs[0][0].join(' '), QStringLiteral("workspace-layout 0 1 bsp"));
}

TEST(ActiveLayoutService, CycleUndoesABlockingState) {
  KomorebiHarness h;
  h.publish(QStringLiteral("connect"), stateWith("BSP", true, true));
  h.komorebi->cycleLayout(0, {QStringLiteral("bsp")}, 1);
  ASSERT_EQ(h.runs.size(), 1u);
  ASSERT_EQ(h.runs[0].size(), 2u);
  EXPECT_EQ(h.runs[0][0].join(' '), QStringLiteral("focus-monitor-at-cursor"));
  EXPECT_EQ(h.runs[0][1].join(' '), QStringLiteral("toggle-pause"));
}

TEST(ActiveLayoutService, NoCommandsWithoutState) {
  KomorebiHarness h;
  h.komorebi->setLayout(0, QStringLiteral("bsp"));
  h.komorebi->cycleLayout(0, {QStringLiteral("bsp")}, 1);
  EXPECT_TRUE(h.runs.empty());
}
