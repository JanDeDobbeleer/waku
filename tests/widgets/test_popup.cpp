#include <gtest/gtest.h>

#include <QApplication>
#include <memory>

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
  qputenv("QT_QPA_PLATFORM", "offscreen");
  int argc = 1;
  char arg0[] = "tests";
  char* argv[] = {arg0, nullptr};
  auto app = std::make_unique<QApplication>(argc, argv);

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
