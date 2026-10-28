#include <gtest/gtest.h>

#include <QRegularExpression>
#include <QWheelEvent>

#include "spike/tests/bar_fixtures.h"

// The built-in widgets (tidy.widgets), instantiated in a real bar on the offscreen platform.
// Volume, Media and Workspaces are left out on purpose: creating them starts real audio,
// media-session, komorebi and GlazeWM clients, which a test must not do on a developer's machine.
// They are covered by the build (qmlcachegen type-checks them) and by running the sample config.

namespace {

QString widgetConfig(const QString& widgets) {
  return QStringLiteral(R"(
import QtQuick
import tidy
import tidy.widgets
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

TEST(ClockWidget, IsInteractiveOnlyWithItsCalendar) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(
      R"(Clock { objectName: "with" }, Clock { objectName: "without"; calendar: false })")));
  ASSERT_TRUE(started(h));
  EXPECT_TRUE(item(h, QStringLiteral("with"))->property("interactive").toBool());
  EXPECT_FALSE(item(h, QStringLiteral("without"))->property("interactive").toBool());
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

TEST(ClockWidget, AClickOpensTheCalendarAndASecondClickClosesIt) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(Clock { objectName: "clock" })")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("calendarPopup"));
  ASSERT_NE(popup, nullptr);
  EXPECT_FALSE(popup->isVisible());

  const QPoint at = centerOf(h, QStringLiteral("clock"));
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier, at);
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->isVisible(); }, 2000));

  QTest::qWait(300);  // past the debounce of the press that closes it
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier, at);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !popup->isVisible(); }, 2000));
  QTest::qWait(400);
  EXPECT_FALSE(popup->isVisible());  // the click that closed it did not reopen it
}

TEST(ClockWidget, TheCalendarOpensOnTheCurrentMonthAndStepsAndResets) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(Clock { objectName: "clock" })")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("calendarPopup"));
  ASSERT_TRUE(QMetaObject::invokeMethod(popup, "open"));
  QQuickItem* calendar = findItem(popup, QStringLiteral("calendar"));
  ASSERT_NE(calendar, nullptr);
  const QDate today = QDate::currentDate();
  EXPECT_EQ(calendar->property("year").toInt(), today.year());
  EXPECT_EQ(calendar->property("month").toInt(), today.month() - 1);  // JavaScript months

  ASSERT_TRUE(QMetaObject::invokeMethod(calendar, "step", Q_ARG(QVariant, 1)));
  EXPECT_EQ(calendar->property("month").toInt(), today.addMonths(1).month() - 1);

  ASSERT_TRUE(QMetaObject::invokeMethod(popup, "close"));
  QTest::qWait(300);
  ASSERT_TRUE(QMetaObject::invokeMethod(popup, "open"));
  EXPECT_EQ(calendar->property("month").toInt(), today.month() - 1);  // reopened: back to now
}

TEST(CalendarWidget, ShowsSixWeeksStartingOnTheLocalesFirstDay) {
  Harness h;
  h.start(widgetConfig(
      QStringLiteral(R"(Calendar { objectName: "cal"; year: 2026; month: 9 })")));  // October 2026
  ASSERT_TRUE(started(h));
  QQuickItem* calendar = item(h, QStringLiteral("cal"));
  const QDate first = calendar->property("firstShown").toDate();
  // The first cell is on the first day of the week and the 1st of the month is in the first row.
  const int firstDayOfWeek = calendar->property("firstDay").toInt();  // 0 = Sunday
  EXPECT_EQ(first.dayOfWeek() % 7, firstDayOfWeek);
  const qint64 offset = first.daysTo(QDate(2026, 10, 1));
  EXPECT_GE(offset, 0);
  EXPECT_LE(offset, 6);
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
