#include <gtest/gtest.h>

#include <QApplication>
#include <memory>
#include <nlohmann/json.hpp>

#include "core/css/transition_engine.h"
#include "core/widgets/popup.h"

using namespace yasb::widgets;

namespace {
// 3840x2560 at 150% scaling, as logical pixels.
const QRect kScreen(0, 0, 2560, 1707);
const QSize kPopup(300, 400);
}  // namespace

TEST(PopupPosition, RightAlignedBelowWithOffset) {
  const QPoint p =
      computePopupPosition({2000, 0}, {100, 36}, kPopup, kScreen, "right", "down", 0, 6);
  EXPECT_EQ(p, QPoint(1800, 42));
}

TEST(PopupPosition, ClampsInsideScreenUsingInclusiveEdges) {
  EXPECT_EQ(computePopupPosition({2500, 0}, {100, 36}, kPopup, kScreen, "right", "down", 0, 6),
            QPoint(2259, 42));
  EXPECT_EQ(computePopupPosition({10, 1690}, {100, 36}, kPopup, kScreen, "left", "down", 0, 6),
            QPoint(10, 1306));
  EXPECT_EQ(computePopupPosition({10, 0}, {100, 36}, kPopup, kScreen, "right", "down", 0, 0).x(),
            0);
}

TEST(PopupPosition, UpwardOpensAboveTheAnchor) {
  EXPECT_EQ(computePopupPosition({100, 1600}, {100, 36}, kPopup, kScreen, "left", "up", 4, 6),
            QPoint(104, 1194));
}

TEST(PopupPosition, CenterUsesFloorDivision) {
  EXPECT_EQ(computePopupPosition({1000, 0}, {100, 36}, kPopup, kScreen, "center", "down", 0, 0),
            QPoint(900, 36));
  // (99 - 300) // 2 is -101 in Python, not -100.
  EXPECT_EQ(computePopupPosition({1000, 0}, {99, 36}, kPopup, kScreen, "center", "down", 0, 0).x(),
            899);
}

TEST(PopupPosition, UnknownAlignmentFallsBackToLeft) {
  EXPECT_EQ(computePopupPosition({50, 0}, {100, 36}, kPopup, kScreen, "weird", "down", 0, 0),
            QPoint(50, 36));
}

TEST(PopupWidgetTest, AnchorsToParentGlobalPosition) {
  QWidget anchor;
  anchor.setGeometry(100, 100, 200, 30);
  auto* popup = new PopupWidget(&anchor);
  popup->resize(100, 50);
  popup->setPosition("left", "down", 0, 6);
  EXPECT_EQ(popup->pos(), QPoint(100, 136));

  popup->resize(120, 60);  // resize re-applies the stored placement
  popup->setPosition("right", "down", 0, 6);
  EXPECT_EQ(popup->pos(), QPoint(180, 136));
}

TEST(PopupWidgetTest, PaintsStylesheetBackgroundFromClassRule) {
  yasb::css::TransitionEngine engine;
  QWidget anchor;
  anchor.setStyleSheet(
      engine.loadStyleSheet(QStringLiteral(".probe-menu { background-color: #282c34; }")));
  anchor.resize(100, 30);
  anchor.show();
  PopupWidget popup(&anchor);
  popup.setProperty("class", QStringLiteral("probe-menu"));
  popup.resize(120, 80);
  popup.show();
  const QColor px = popup.grab().toImage().pixelColor(5, 5);
  EXPECT_EQ(px.name(), QStringLiteral("#282c34"));
  EXPECT_EQ(px.alpha(), 255);
}

#include "core/widgets/tooltip.h"

TEST(PopupWidgetTest, TooltipPaintsStylesheetBackground) {
  yasb::css::TransitionEngine engine;
  QWidget bar;
  bar.setStyleSheet(
      engine.loadStyleSheet(QStringLiteral(".tooltip { background-color: rgb(35, 38, 52); border: "
                                           "1px solid #414559; padding: 5px 10px; }")));
  bar.show();
  CustomToolTip tip(false, &bar);
  tip.showTooltip(QStringLiteral("hello"), QRect(10, 10, 20, 20), QRect(0, 0, 200, 30), 0);
  const QColor px = tip.grab().toImage().pixelColor(4, 4);
  EXPECT_EQ(px.name(), QStringLiteral("#232634"));
  EXPECT_EQ(px.alpha(), 255);
}

TEST(TooltipOptionsTest, ParsesTopLevelTooltipBlock) {
  const auto root = nlohmann::json::parse(
      R"({"tooltip":{"offset":9,"blur_effect":{"enabled":true,"dark_mode":true,)"
      R"("round_corners":true,"round_corners_type":"small","border_color":"#112233"}}})");
  const auto options = yasb::widgets::parseTooltipOptions(root);
  EXPECT_EQ(options.offset, 9);
  EXPECT_TRUE(options.blur.enabled);
  EXPECT_TRUE(options.blur.darkMode);
  EXPECT_TRUE(options.blur.roundCorners);
  EXPECT_EQ(options.blur.roundCornersType, QStringLiteral("small"));
  EXPECT_EQ(options.blur.borderColor, QStringLiteral("#112233"));
}

TEST(TooltipOptionsTest, MissingBlockUsesPythonDefaults) {
  const auto options = yasb::widgets::parseTooltipOptions(nlohmann::json::object());
  EXPECT_EQ(options.offset, 5);
  EXPECT_FALSE(options.blur.enabled);
  EXPECT_EQ(options.blur.roundCornersType, QStringLiteral("normal"));
  EXPECT_EQ(options.blur.borderColor, QStringLiteral("None"));
}
