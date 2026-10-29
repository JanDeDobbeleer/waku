#include <gtest/gtest.h>

#include <QRegularExpression>
#include <QWheelEvent>

#include "spike/tests/bar_fixtures.h"

// The built-in widgets (wabar.widgets), instantiated in a real bar on the offscreen platform.
// Volume, Media and Workspaces are left out on purpose: creating them starts real audio,
// media-session, komorebi and GlazeWM clients, which a test must not do on a developer's machine.
// They are covered by the build (qmlcachegen type-checks them) and by running the sample config.

namespace {

QString widgetConfig(const QString& widgets) {
  return QStringLiteral(R"(
import QtQuick
import wabar
import wabar.widgets
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

TEST(CpuWidget, AClickOpensThePopupWithGraphAndCards) {
  Harness h;
  h.start(
      widgetConfig(QStringLiteral(R"(Cpu { objectName: "cpu"; interval: 200; format: "%1%" })")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("cpuPopup"));
  ASSERT_NE(popup, nullptr);
  EXPECT_FALSE(popup->isVisible());
  QQuickItem* cpu = item(h, QStringLiteral("cpu"));
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return !cpu->property("text").toString().contains(u'…'); }, 5000));
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier, centerOf(h, QStringLiteral("cpu")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->isVisible(); }, 2000));
  QQuickItem* graph = findItem(popup, QStringLiteral("statGraph"));
  ASSERT_NE(graph, nullptr);
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return graph->property("points").toList().size() >= 2; }, 5000));
  EXPECT_NE(findItem(popup, QStringLiteral("statValue3")), nullptr);  // four cards
  EXPECT_EQ(findItem(popup, QStringLiteral("statValue4")), nullptr);
}

TEST(CpuWidget, ARightClickTogglesTheAlternativeLabel) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(
      R"(Cpu { objectName: "cpu"; interval: 200; format: "load %1"; altFormat: "alt %1" })")));
  ASSERT_TRUE(started(h));
  const QRegularExpression load(QStringLiteral(R"(^load \d+$)"));
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return load.match(text(h, QStringLiteral("cpu"))).hasMatch(); }, 5000));
  QTest::mouseClick(h.window(), Qt::RightButton, Qt::NoModifier,
                    centerOf(h, QStringLiteral("cpu")));
  const QRegularExpression alt(QStringLiteral(R"(^alt \d+$)"));
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return alt.match(text(h, QStringLiteral("cpu"))).hasMatch(); }, 2000));
  QQuickWindow* popup = findWindow(QStringLiteral("cpuPopup"));
  ASSERT_NE(popup, nullptr);
  EXPECT_FALSE(popup->isVisible());
}

TEST(MemoryWidget, AClickOpensThePopupWithSixCards) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(R"(Memory { objectName: "mem"; interval: 200 })")));
  ASSERT_TRUE(started(h));
  QQuickWindow* popup = findWindow(QStringLiteral("memoryPopup"));
  ASSERT_NE(popup, nullptr);
  QQuickItem* mem = item(h, QStringLiteral("mem"));
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return !mem->property("text").toString().contains(u'…'); }, 5000));
  QTest::mouseClick(h.window(), Qt::LeftButton, Qt::NoModifier, centerOf(h, QStringLiteral("mem")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->isVisible(); }, 2000));
  EXPECT_NE(findItem(popup, QStringLiteral("statValue5")), nullptr);
  EXPECT_EQ(findItem(popup, QStringLiteral("statValue6")), nullptr);
}

TEST(MemoryWidget, ARightClickTogglesTheAlternativeLabel) {
  Harness h;
  h.start(widgetConfig(QStringLiteral(
      R"(Memory { objectName: "mem"; interval: 200; format: "a%1"; altFormat: "b%1" })")));
  ASSERT_TRUE(started(h));
  ASSERT_TRUE(QTest::qWaitFor(
      [&] {
        const QString t = text(h, QStringLiteral("mem"));
        return t.startsWith('a') && !t.contains(u'…');
      },
      5000));
  QTest::mouseClick(h.window(), Qt::RightButton, Qt::NoModifier,
                    centerOf(h, QStringLiteral("mem")));
  EXPECT_TRUE(
      QTest::qWaitFor([&] { return text(h, QStringLiteral("mem")).startsWith('b'); }, 2000));
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
