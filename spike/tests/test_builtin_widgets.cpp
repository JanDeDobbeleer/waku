#include <gtest/gtest.h>

#include <QRegularExpression>
#include <QWheelEvent>

#include "spike/tests/bar_fixtures.h"

// The built-in widgets (barnacle.widgets), instantiated in a real bar on the offscreen platform.
// Volume, Media and Workspaces are left out on purpose: creating them starts real audio,
// media-session, komorebi and GlazeWM clients, which a test must not do on a developer's machine.
// They are covered by the build (qmlcachegen type-checks them) and by running the sample config.

namespace {

QString widgetConfig(const QString& widgets) {
  return QStringLiteral(R"(
import QtQuick
import barnacle
import barnacle.widgets
Config {
    Bar {
        layout: BarLayout {
            start: [ %1 ]
        }
    }
}
)")
      .arg(widgets);
}

QQuickItem* item(Harness& h, const QString& name) { return findItem(h.window(), name); }

QString text(Harness& h, const QString& name) {
  QQuickItem* i = item(h, name);
  return i ? i->property("text").toString() : QStringLiteral("<missing>");
}

bool started(Harness& h) {
  if (!h.host->reload()) return false;
  QQuickWindow* window = h.window();
  return window && QTest::qWaitForWindowExposed(window);
}

QPoint centerOf(Harness& h, const QString& name) {
  QQuickItem* i = item(h, name);
  return i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint();
}

}  // namespace

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

TEST(ClockWidget, FormatsTheTimeWithTheGivenFormat) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(Clock { objectName: "clock"; format: "HH:mm:ss" })")));
  ASSERT_TRUE(started(h));
  QQuickItem* clock = item(h, QStringLiteral("clock"));
  const QDateTime now = clock->property("now").toDateTime();
  EXPECT_LT(qAbs(now.secsTo(QDateTime::currentDateTime())), 3);
  EXPECT_EQ(clock->property("text").toString(), now.toString(QStringLiteral("HH:mm:ss")));
}

TEST(ClockWidget, ShowsSecondsOnlyWhenTheFormatHasThem) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(
      R"(Clock { objectName: "with"; format: "HH:mm:ss" }, Clock { objectName: "without"; format: "HH:mm" })")));
  ASSERT_TRUE(started(h));
  EXPECT_TRUE(item(h, QStringLiteral("with"))->property("showsSeconds").toBool());
  EXPECT_FALSE(item(h, QStringLiteral("without"))->property("showsSeconds").toBool());
}

TEST(ClockWidget, KeepsTicking) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(Clock { objectName: "clock"; format: "ss" })")));
  ASSERT_TRUE(started(h));
  QQuickItem* clock = item(h, QStringLiteral("clock"));
  const QDateTime first = clock->property("now").toDateTime();
  ASSERT_TRUE(QTest::qWaitFor([&] { return clock->property("now").toDateTime() > first; }, 3000));
  EXPECT_EQ(clock->property("text").toString(),
            clock->property("now").toDateTime().toString(QStringLiteral("ss")));
}

TEST(ClockWidget, IsNotInteractiveByDefault) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(Clock { objectName: "clock" })")));
  ASSERT_TRUE(started(h));
  EXPECT_FALSE(item(h, QStringLiteral("clock"))->property("interactive").toBool());
}

TEST(CpuWidget, ShowsTheLoadOnceSampled) {
  Harness h;
  h.start(widgetConfig(
      QStringLiteral(R"(Cpu { objectName: "cpu"; interval: 200; format: "load %1 pct" })")));
  ASSERT_TRUE(started(h));
  const QRegularExpression loaded(QStringLiteral(R"(^load \d{1,3} pct$)"));
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return loaded.match(text(h, QStringLiteral("cpu"))).hasMatch(); }, 5000))
      << qPrintable(text(h, QStringLiteral("cpu")));
}

TEST(CpuWidget, TurnsToTheWarningColourAtTheThreshold) {
  Harness h;
  // A threshold below zero is always crossed, whatever the load.
  h.start(widgetConfig(QStringLiteral(
      R"(Cpu { objectName: "cpu"; interval: 200; warnAbove: -1; warnFill: "#112233" })")));
  ASSERT_TRUE(started(h));
  QQuickItem* cpu = item(h, QStringLiteral("cpu"));
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return cpu->property("fill").value<QColor>() == QColor(QStringLiteral("#112233")); },
      5000));
}

TEST(MemoryWidget, ReplacesAllThreePlaceholders) {
  Harness h;
  h.start(widgetConfig(
      QStringLiteral(R"(Memory { objectName: "mem"; interval: 200; format: "%1|%2|%3" })")));
  ASSERT_TRUE(started(h));
  const QRegularExpression loaded(QStringLiteral(R"(^\d{1,3}\|\d+\.\d\|\d+\.\d$)"));
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return loaded.match(text(h, QStringLiteral("mem"))).hasMatch(); }, 5000))
      << qPrintable(text(h, QStringLiteral("mem")));
}

TEST(BatteryWidget, IsHiddenExactlyWhenThereIsNoBattery) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(Battery { objectName: "bat"; interval: 200 })")));
  ASSERT_TRUE(started(h));
  QQuickItem* battery = item(h, QStringLiteral("bat"));
  auto* source = battery->property("source").value<QObject*>();
  ASSERT_NE(source, nullptr);
  // The first sample decides; afterwards the widget follows the service.
  ASSERT_TRUE(QTest::qWaitFor([&] { return source->property("ready").toBool(); }, 5000));
  EXPECT_EQ(battery->property("visible").toBool(), source->property("available").toBool());
}
