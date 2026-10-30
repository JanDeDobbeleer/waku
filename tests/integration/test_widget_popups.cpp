// The popups of built-in widgets of several features (ControlCenter, Media) in a real bar on the
// offscreen platform.
#include <gtest/gtest.h>

#include "host/testing/widget_fixtures.h"

namespace {

void invoke(QObject* object, const char* method) {
  ASSERT_TRUE(QMetaObject::invokeMethod(object, method));
}

}  // namespace

TEST(WidgetPopupsTest, TheControlCenterPanelIs360WideAndTheMediaMenu400) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
import wabar.widgets
ShellRoot {
    Panel {
        content: PanelLayout {
            start: [ ControlCenter { }, Media { } ]
        }
    }
}
)"));
  ASSERT_TRUE(started(h));
  QQuickWindow* control = findWindow(QStringLiteral("controlCenterPopup"));
  ASSERT_NE(control, nullptr);
  invoke(control, "open");
  QTest::qWait(100);
  EXPECT_EQ(control->width(), 360);
  EXPECT_GT(control->height(), 100);

  QQuickWindow* media = findWindow(QStringLiteral("mediaPopup"));
  ASSERT_NE(media, nullptr);
  invoke(media, "open");
  QTest::qWait(100);
  EXPECT_EQ(media->width(), 400);
}
