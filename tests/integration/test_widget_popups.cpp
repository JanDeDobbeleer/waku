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
import waku
import waku.widgets
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
  // The platform reports the new geometry asynchronously.
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return control->width() == 360 && control->height() > 100; }, 2000));
  EXPECT_EQ(control->width(), 360);
  EXPECT_GT(control->height(), 100);

  QQuickWindow* media = findWindow(QStringLiteral("mediaPopup"));
  ASSERT_NE(media, nullptr);
  // A hidden panel does not follow the playback position.
  EXPECT_EQ(findItem(media, QStringLiteral("mediaPosition"))->property("text").toString(),
            QString());
  invoke(media, "open");
  EXPECT_TRUE(QTest::qWaitFor([&] { return media->width() == 400; }, 2000));
  EXPECT_EQ(media->width(), 400);
  EXPECT_EQ(findItem(media, QStringLiteral("mediaPosition"))->property("text").toString(),
            QStringLiteral("0:00"));
}
