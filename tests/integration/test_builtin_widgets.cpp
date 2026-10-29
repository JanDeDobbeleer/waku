// The shared look (Pill, Slider) and the tooltips of the built-in widgets of several features,
// instantiated in a real bar on the offscreen platform. Volume, Media and Workspaces are left out
// on purpose: creating them starts real audio, media-session, komorebi and GlazeWM clients, which
// a test must not do on a developer's machine.
#include <gtest/gtest.h>

#include <QWheelEvent>

#include "host/testing/widget_fixtures.h"

TEST(PillWidget, ShowsItsTextAndGrowsWithIt) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(
      R"(Pill { objectName: "short"; text: "hi" }, Pill { objectName: "long"; text: "hello there" })")));
  ASSERT_TRUE(started(h));
  EXPECT_EQ(text(h, QStringLiteral("short")), QStringLiteral("hi"));
  EXPECT_GT(item(h, QStringLiteral("long"))->implicitWidth(),
            item(h, QStringLiteral("short"))->implicitWidth());
  EXPECT_EQ(item(h, QStringLiteral("short"))->implicitHeight(), 24);
}

TEST(PillWidget, ClicksAndWheelReachItsSignals) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(
Pill {
    objectName: "p"; text: "press"
    property int clicks: 0
    property int rightClicks: 0
    property int lastDelta: 0
    onClicked: clicks++
    onRightClicked: rightClicks++
    onScrolled: delta => lastDelta = delta
})")));
  ASSERT_TRUE(started(h));
  QQuickItem* pill = item(h, QStringLiteral("p"));
  const QPoint at = centerOf(h, QStringLiteral("p"));

  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier, at);
  QTest::mouseClick(h.window(), Qt::RightButton, Qt::NoModifier, at);
  EXPECT_EQ(pill->property("clicks").toInt(), 1);
  EXPECT_EQ(pill->property("rightClicks").toInt(), 1);

  QWheelEvent wheel(QPointF(at), QPointF(at), QPoint(), QPoint(0, 120), Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(h.window(), &wheel);
  EXPECT_EQ(pill->property("lastDelta").toInt(), 120);
}

TEST(PillWidget, ANonInteractivePillIgnoresInput) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(
Pill { objectName: "p"; text: "static"; interactive: false
       property int clicks: 0
       onClicked: clicks++ })")));
  ASSERT_TRUE(started(h));
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier, centerOf(h, QStringLiteral("p")));
  EXPECT_EQ(item(h, QStringLiteral("p"))->property("clicks").toInt(), 0);
}

TEST(PillWidget, OnABarItIsFlatAndTakesTheBarFill) {
  Harness h;
  write(h.dir.filePath(QStringLiteral("theme.qml")),
        QStringLiteral(
            "import wabar\nThemeSpec { barFill: \"#00000000\"; barFillHover: \"#26c6d0f5\" }\n"));
  h.start(widgetConfig(QStringLiteral(R"(Pill { objectName: "p"; text: "x" })")));
  ASSERT_TRUE(started(h));
  QQuickItem* pill = item(h, QStringLiteral("p"));
  EXPECT_TRUE(pill->property("flat").toBool());
  EXPECT_EQ(pill->property("fill").value<QColor>().alpha(), 0);
  EXPECT_EQ(pill->property("hoverFill").value<QColor>(), QColor(0xc6, 0xd0, 0xf5, 0x26));
  EXPECT_EQ(pill->property("color").value<QColor>().alpha(), 0);
}

TEST(PillWidget, InAPopupItKeepsTheVisibleFill) {
  Harness h;
  write(h.dir.filePath(QStringLiteral("theme.qml")),
        QStringLiteral("import wabar\nThemeSpec { barFill: \"#00000000\" }\n"));
  h.start(widgetConfig(QStringLiteral(R"(
Pill {
    objectName: "anchor"; text: "a"
    PopupWindow {
        anchorItem: parent
        Pill { objectName: "row"; text: "row" }
    }
})")));
  ASSERT_TRUE(started(h));
  QQuickItem* anchor = item(h, QStringLiteral("anchor"));
  EXPECT_EQ(anchor->property("fill").value<QColor>().alpha(), 0);
  QQuickItem* row = nullptr;
  for (QWindow* w : QGuiApplication::allWindows()) {
    if (auto* q = qobject_cast<QQuickWindow*>(w))
      if (QQuickItem* found = findItem(q, QStringLiteral("row"))) row = found;
  }
  ASSERT_NE(row, nullptr);
  EXPECT_FALSE(row->property("flat").toBool());
  EXPECT_EQ(row->property("fill").value<QColor>(), QColor(0xff, 0xff, 0xff, 0x22));
}

namespace {
// The Text item with this text under `root`.
QQuickItem* textChild(QQuickItem* root, const QString& text) {
  for (QQuickItem* child : root->childItems()) {
    if (child->property("text").toString() == text && child->property("font").isValid() &&
        child->property("elide").isValid())
      return child;
    if (QQuickItem* found = textChild(child, text)) return found;
  }
  return nullptr;
}
}  // namespace

TEST(PillWidget, AnIconIsDrawnInTheIconFont) {
  Harness h;
  write(h.dir.filePath(QStringLiteral("theme.qml")),
        QStringLiteral("import wabar\nThemeSpec { iconFont: \"Test Icons\"; iconSize: 18 }\n"));
  h.start(widgetConfig(QStringLiteral(R"(Pill { objectName: "p"; icon: ""; text: "hi" })")));
  ASSERT_TRUE(started(h));
  QQuickItem* pill = item(h, QStringLiteral("p"));
  QQuickItem* glyph = textChild(pill, QString(QChar(0xe702)));
  ASSERT_NE(glyph, nullptr);
  const QFont font = glyph->property("font").value<QFont>();
  EXPECT_EQ(font.family(), QStringLiteral("Test Icons"));
  EXPECT_EQ(font.pixelSize(), 18);
  QQuickItem* label = textChild(pill, QStringLiteral("hi"));
  ASSERT_NE(label, nullptr);
  EXPECT_NE(label->property("font").value<QFont>().family(), QStringLiteral("Test Icons"));
}

TEST(SliderWidget, AClickMovesToThePositionAndReportsIt) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(
Slider {
    objectName: "s"; width: 200; from: 0; to: 100; value: 10
    property real got: -1
    onMoved: v => got = v
})")));
  ASSERT_TRUE(started(h));
  QQuickItem* slider = item(h, QStringLiteral("s"));
  const QPointF origin = slider->mapToScene(QPointF(0, 0));
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier,
                    QPoint(qRound(origin.x() + 50), qRound(origin.y() + slider->height() / 2)));
  EXPECT_NEAR(slider->property("got").toDouble(), 25, 1.0);    // a quarter of 200 px
  EXPECT_DOUBLE_EQ(slider->property("value").toDouble(), 10);  // the owner decides the value
}

TEST(SliderWidget, TheRatioIsClampedIntoTheRange) {
  Harness h;
  h.start(
      widgetConfig(QStringLiteral(R"(Slider { objectName: "s"; from: 10; to: 20; value: 50 })")));
  ASSERT_TRUE(started(h));
  EXPECT_DOUBLE_EQ(item(h, QStringLiteral("s"))->property("ratio").toDouble(), 1.0);
}

TEST(Tooltips, TheBuiltInWidgetsDescribeThemselves) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(
Clock { objectName: "clock"; tooltipFormat: "yyyy" },
Cpu { objectName: "cpu"; interval: 200 },
Memory { objectName: "mem"; interval: 200 })")));
  ASSERT_TRUE(started(h));
  EXPECT_EQ(item(h, QStringLiteral("clock"))->property("tooltip").toString(),
            QString::number(QDate::currentDate().year()));
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return !item(h, QStringLiteral("cpu"))->property("tooltip").toString().isEmpty(); },
      5000));
  const QString cpu = item(h, QStringLiteral("cpu"))->property("tooltip").toString();
  EXPECT_TRUE(cpu.contains(QStringLiteral("cores"))) << qPrintable(cpu);
  EXPECT_TRUE(cpu.contains(QLatin1Char('\n'))) << qPrintable(cpu);  // the per-core line
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return !item(h, QStringLiteral("mem"))->property("tooltip").toString().isEmpty(); },
      5000));
  const QString mem = item(h, QStringLiteral("mem"))->property("tooltip").toString();
  EXPECT_TRUE(mem.startsWith(QStringLiteral("Used "))) << qPrintable(mem);
  EXPECT_TRUE(mem.contains(QStringLiteral("Swap"))) << qPrintable(mem);
}

TEST(Tooltips, APillShowsItsTooltipOnHover) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(Pill { objectName: "p"; text: "x"; tooltip: "hello" })")));
  ASSERT_TRUE(started(h));
  QQuickWindow* tip = findWindow(QStringLiteral("tooltipWindow"));
  ASSERT_NE(tip, nullptr);
  QTest::mouseMove(h.window(), QPoint(h.window()->width() - 5, h.window()->height() - 3));
  QTest::qWait(20);
  QTest::mouseMove(h.window(), centerOf(h, QStringLiteral("p")));
  EXPECT_TRUE(QTest::qWaitFor([&] { return tip->isVisible(); }, 3000));
}
