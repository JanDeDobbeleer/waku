#include <gtest/gtest.h>

#include <QApplication>
#include <cmath>

#include "core/widgets/scrolling_label.h"

using namespace yasb::widgets;

namespace {

ScrollingLabelOptions optionsFor(ScrollingLabelOptions::Style style) {
  ScrollingLabelOptions options;
  options.style = style;
  options.updateIntervalMs = 20;
  return options;
}

const QString kLong =
    QStringLiteral("A long string that is certainly wider than the tiny label it is shown in");

}  // namespace

TEST(ScrollingLabelOptions, ParsesStyleNames) {
  using Style = ScrollingLabelOptions::Style;
  EXPECT_EQ(ScrollingLabelOptions::parseStyle(QStringLiteral("left")), Style::Left);
  EXPECT_EQ(ScrollingLabelOptions::parseStyle(QStringLiteral("right")), Style::Right);
  EXPECT_EQ(ScrollingLabelOptions::parseStyle(QStringLiteral("bounce")), Style::Bounce);
  EXPECT_EQ(ScrollingLabelOptions::parseStyle(QStringLiteral("bounce-ease")), Style::BounceEase);
  EXPECT_EQ(ScrollingLabelOptions::parseStyle(QString()), Style::Left);
  EXPECT_EQ(ScrollingLabelOptions::parseStyle(QStringLiteral("nonsense")), Style::Left);
}

TEST(ScrollEase, IsOneInTheMiddleAndMinAtTheEnds) {
  EXPECT_NEAR(scrollEase(50, 100, 20, 0.8, 0.5), 1.0, 1e-6);
  EXPECT_NEAR(scrollEase(0, 100, 20, 0.8, 0.5), 0.5, 1e-3);
  EXPECT_NEAR(scrollEase(100, 100, 20, 0.8, 0.5), 0.5, 1e-3);
  EXPECT_GT(scrollEase(30, 100, 20, 0.8, 0.5), 0.5);
  // A zero range gives x = 0, the centre.
  EXPECT_NEAR(scrollEase(0, 0, 20, 0.8, 0.5), 1.0, 1e-6);
}

TEST(ScrollingLabel, ShortTextDoesNotScroll) {
  ScrollingLabel label(nullptr, 0, optionsFor(ScrollingLabelOptions::Style::Left));
  label.resize(400, 24);
  label.setText(QStringLiteral("Hi"));
  EXPECT_FALSE(label.scrollingNeeded());
  EXPECT_EQ(label.displayText(), QStringLiteral("Hi"));
  EXPECT_EQ(label.offset(), 0);
  label.show();
  EXPECT_FALSE(label.timerActive());
}

TEST(ScrollingLabel, LongTextScrollsLeftWithSeparator) {
  auto options = optionsFor(ScrollingLabelOptions::Style::Left);
  options.separator = QStringLiteral(" | ");
  ScrollingLabel label(nullptr, 0, options);
  label.resize(60, 24);
  label.show();
  label.setText(kLong);
  ASSERT_TRUE(label.scrollingNeeded());
  EXPECT_EQ(label.displayText(), kLong + QStringLiteral(" | "));
  EXPECT_TRUE(label.timerActive());
  EXPECT_EQ(label.offset(), 1);  // setText already took one step, as in Python
  label.advance();
  label.advance();
  EXPECT_EQ(label.offset(), 3);
}

TEST(ScrollingLabel, RightScrollMovesBackwards) {
  ScrollingLabel label(nullptr, 0, optionsFor(ScrollingLabelOptions::Style::Right));
  label.resize(60, 24);
  label.show();
  label.setText(kLong);
  ASSERT_TRUE(label.scrollingNeeded());
  EXPECT_EQ(label.offset(), -1);
  label.advance();
  EXPECT_EQ(label.offset(), -2);
}

TEST(ScrollingLabel, AlwaysScrollMakesShortTextScroll) {
  auto options = optionsFor(ScrollingLabelOptions::Style::Left);
  options.alwaysScroll = true;
  ScrollingLabel label(nullptr, 0, options);
  label.resize(400, 24);
  label.show();
  label.setText(QStringLiteral("Hi"));
  EXPECT_TRUE(label.scrollingNeeded());
  EXPECT_TRUE(label.timerActive());
}

TEST(ScrollingLabel, BouncePadsWithoutSeparatorAndPingPongs) {
  auto options = optionsFor(ScrollingLabelOptions::Style::Bounce);
  options.labelPadding = 2;
  ScrollingLabel label(nullptr, 0, options);
  label.resize(60, 24);
  label.show();
  label.setText(kLong);
  ASSERT_TRUE(label.scrollingNeeded());
  EXPECT_EQ(label.displayText(), QStringLiteral("  ") + kLong + QStringLiteral("  "));
  // The bounce starts heading towards offset 0, turns around, then moves right.
  EXPECT_EQ(label.offset(), 0);
  label.advance();
  EXPECT_EQ(label.offset(), 1);
  label.advance();
  EXPECT_EQ(label.offset(), 2);

  ScrollingLabel shortLabel(nullptr, 0, options);
  shortLabel.resize(400, 24);
  shortLabel.setText(QStringLiteral("Hi"));
  EXPECT_FALSE(shortLabel.scrollingNeeded());
  EXPECT_LE(shortLabel.offset(), 0);  // centred: (textWidth - labelWidth) / 2
}

TEST(ScrollingLabel, TimerFollowsVisibilityAndMaxWidthCapsTheLabel) {
  ScrollingLabel label(nullptr, 5, optionsFor(ScrollingLabelOptions::Style::Left));
  label.resize(60, 24);
  label.setText(kLong);
  EXPECT_FALSE(label.timerActive());  // hidden
  label.show();
  EXPECT_TRUE(label.timerActive());
  label.hide();
  EXPECT_FALSE(label.timerActive());
  EXPECT_LE(label.sizeHint().width(), label.maximumWidth());
  EXPECT_GT(label.maximumWidth(), 0);
  EXPECT_LT(label.maximumWidth(), QWIDGETSIZE_MAX);
}

TEST(ScrollingLabel, ResizingWiderStopsTheScroll) {
  ScrollingLabel label(nullptr, 0, optionsFor(ScrollingLabelOptions::Style::Left));
  label.resize(60, 24);
  label.show();
  label.setText(QStringLiteral("Some medium text"));
  ASSERT_TRUE(label.scrollingNeeded());
  label.resize(600, 24);
  EXPECT_FALSE(label.scrollingNeeded());
  EXPECT_FALSE(label.timerActive());
  EXPECT_EQ(label.offset(), 0);
}
