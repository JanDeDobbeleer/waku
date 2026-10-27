#include <gtest/gtest.h>

#include <QApplication>
#include <memory>

#include "core/widgets/backdrop.h"
#include "core/widgets/stat_popup.h"
#include "core/widgets/tooltip.h"
#include "core/widgets/widget_builder.h"

using namespace yasb::widgets;

namespace {

struct OffscreenApp {
  OffscreenApp() {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    app = std::make_unique<QApplication>(argc, argv);
  }
  int argc = 1;
  char arg0[8] = "tests";
  char* argv[2] = {arg0, nullptr};
  std::unique_ptr<QApplication> app;
};

const QRect kScreen(0, 0, 2560, 1707);

}  // namespace

TEST(Backdrop, HexColorIsByteSwappedForDwm) {
  EXPECT_EQ(hexToRgbaInt("#112233FF"), 0xFF332211u);
  EXPECT_THROW(hexToRgbaInt("112233"), std::invalid_argument);
  EXPECT_THROW(hexToRgbaInt("#GG2233FF"), std::invalid_argument);
}

TEST(StatPopup, GraphPointsAndSplineShape) {
  const auto pts = graphPoints({0, 100}, 100, 22);
  ASSERT_EQ(pts.size(), 2u);
  EXPECT_EQ(pts[0], QPointF(0, 21));
  EXPECT_EQ(pts[1], QPointF(100, 1));
  EXPECT_EQ(buildSmoothPath(pts).elementCount(), 2);  // two points stay a straight line

  const auto three = graphPoints({10, 90, 10}, 100, 22);
  EXPECT_EQ(buildSmoothPath(three).elementCount(), 1 + 2 * 3);
  EXPECT_TRUE(graphPoints({}, 100, 22).empty());
  EXPECT_EQ(graphPoints({150}, 100, 22)[0].y(), 1.0);  // values clamp to 0-100
}

TEST(Tooltip, PositionPrefersBelowThenAbove) {
  const QRect widget(100, 0, 50, 30);
  EXPECT_EQ(computeTooltipPosition(widget, widget, {80, 20}, kScreen, "", 5), QPoint(84, 34));
  EXPECT_EQ(computeTooltipPosition(widget, widget, {80, 20}, kScreen, "top", 5),
            QPoint(84, 34));  // no room above

  const QRect low(100, 1690, 50, 16);
  EXPECT_EQ(computeTooltipPosition(low, low, {80, 20}, kScreen, "", 5), QPoint(84, 1665));
  EXPECT_EQ(
      computeTooltipPosition(QRect(0, 0, 10, 10), QRect(0, 0, 10, 10), {80, 20}, kScreen, "", 5)
          .x(),
      0);
}

TEST(StatPopup, BuildsStructureAndPinsOnToggle) {
  OffscreenApp env;
  QWidget anchor;
  anchor.setGeometry(100, 100, 200, 30);
  StatPopupMenuConfig menu;
  menu.blur = false;
  menu.pinIcon = "P";
  menu.unpinIcon = "U";
  const std::vector<StatRow> rows = {
      {QString("Total"), "total", "1G", QString("Used"), "used", "2G"},
      {QString("Free"), "free", "3G", std::nullopt, "", ""}};
  PinnablePopup* popup =
      buildStatPopup(&anchor, menu, "memory-popup", "Memory", {10, 20, 30}, rows);

  EXPECT_EQ(popup->property("class").toString(), "memory-popup");
  ASSERT_NE(popup->graph, nullptr);
  EXPECT_EQ(popup->statLabels.size(), 3);
  EXPECT_EQ(popup->statLabels.value("used")->text(), "2G");
  EXPECT_EQ(popup->pinButton->property("class").toString(), "pin-btn");

  popup->pinButton->setChecked(true);
  EXPECT_TRUE(popup->isPinned());
  EXPECT_EQ(popup->pinButton->text(), "U");
  EXPECT_EQ(popup->pinButton->property("class").toString(), "pin-btn pinned");
  popup->pinButton->setChecked(false);
  EXPECT_FALSE(popup->isPinned());
  EXPECT_EQ(popup->pinButton->text(), "P");
  delete popup;
}

TEST(WidgetBuilder, BuildsKnownWidgetsAndRemembersFailuresOnce) {
  OffscreenApp env;
  yasb::config::LoadResult cfg;
  auto add = [&](const char* name, yasb::config::WidgetStatus status, const char* type) {
    yasb::config::WidgetResult r;
    r.status = status;
    r.type = type;
    cfg.widgets[name] = r;
  };
  add("ok", yasb::config::WidgetStatus::Ok, "test.Widget");
  add("bad", yasb::config::WidgetStatus::InvalidOptions, "test.Widget");
  add("odd", yasb::config::WidgetStatus::UnknownType, "nope");
  add("unreg", yasb::config::WidgetStatus::Ok, "test.Unregistered");

  WidgetRegistry registry;
  registry.add("test.Widget", [](const WidgetContext& c) {
    return new BaseWidget(QString::fromStdString(c.name), "test-widget ", 0);
  });
  WidgetBuilder builder(cfg, registry);
  const WidgetBuilder::Columns columns = {{"left", {"ok", "bad", "ghost"}},
                                          {"right", {"odd", "ok", "unreg", "bad"}}};
  auto built = builder.buildWidgets(columns, "DISPLAY1", "bar-1");

  ASSERT_EQ(built["left"].size(), 1);
  ASSERT_EQ(built["right"].size(), 1);
  EXPECT_EQ(built["left"][0]->property("screen_name").toString(), "DISPLAY1");
  EXPECT_EQ(built["left"][0]->property("widget_name").toString(), "ok");
  EXPECT_EQ(builder.invalidNames(), std::set<std::string>{"ghost"});
  EXPECT_EQ(builder.invalidOptions(), std::set<std::string>{"bad"});
  EXPECT_EQ(builder.unknownTypes(), (std::set<std::string>{"odd", "unreg"}));
  EXPECT_EQ(builder.errorReport().size(), 4u);
  qDeleteAll(built["left"]);
  qDeleteAll(built["right"]);
}

TEST(WidgetBuilder, ThrowingFactoryIsSkippedInsteadOfEscaping) {
  OffscreenApp env;
  yasb::config::LoadResult cfg;
  yasb::config::WidgetResult r;
  r.status = yasb::config::WidgetStatus::Ok;
  r.type = "test.Throwing";
  cfg.widgets["boom"] = r;
  WidgetRegistry registry;
  registry.add("test.Throwing", [](const WidgetContext& c) -> BaseWidget* {
    return new BaseWidget(QString::fromStdString(c.options.at("missing").get<std::string>()), "",
                          0);
  });
  WidgetBuilder builder(cfg, registry);
  EXPECT_EQ(builder.buildWidget("boom", "DISPLAY1", "bar-1"), nullptr);
}
