// The WorkspaceDot widget in a real bar on the offscreen platform.
#include <gtest/gtest.h>

#include "host/testing/widget_fixtures.h"

TEST(WorkspaceDotWidget, TheFocusedOneIsAWideAccentPill) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(
WorkspaceDot { objectName: "on"; focused: true },
WorkspaceDot { objectName: "used"; hasWindows: true },
WorkspaceDot { objectName: "empty" })")));
  ASSERT_TRUE(started(h));
  QQuickItem* on = item(h, QStringLiteral("on"));
  QQuickItem* used = item(h, QStringLiteral("used"));
  QQuickItem* empty = item(h, QStringLiteral("empty"));
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return on->implicitWidth() > used->implicitWidth() + 10; }, 3000));
  EXPECT_EQ(on->property("restColor").value<QColor>().rgb(), QColor(0x5b, 0x8d, 0xef).rgb());
  EXPECT_GT(used->property("restColor").value<QColor>().alpha(),
            empty->property("restColor").value<QColor>().alpha());
  EXPECT_EQ(used->implicitWidth(), empty->implicitWidth());
}
