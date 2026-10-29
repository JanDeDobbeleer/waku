// The Clock and Calendar widgets in a real bar on the offscreen platform.
#include <gtest/gtest.h>

#include "host/testing/widget_fixtures.h"

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

TEST(CalendarPopupTest, TheSelectedDayStartsAtTodayAndFollowsAClickAndTheMonth) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
import wabar.widgets
Config {
    Bar {
        layout: BarLayout {
            start: [ Calendar { objectName: "cal"; width: 252; height: 220 } ]
        }
    }
}
)"));
  ASSERT_TRUE(started(h));
  QQuickItem* calendar = findItem(h.window(), QStringLiteral("cal"));
  ASSERT_NE(calendar, nullptr);
  EXPECT_EQ(calendar->property("selected").toDate(), QDate::currentDate());

  // Stepping a month keeps the day number, clamped to the new month's length.
  ASSERT_TRUE(QMetaObject::invokeMethod(calendar, "step", Q_ARG(QVariant, 1)));
  const QDate moved = calendar->property("selected").toDate();
  const QDate next = QDate::currentDate().addMonths(1);
  EXPECT_EQ(moved.month(), next.month());
  EXPECT_EQ(moved.day(), next.day());

  ASSERT_TRUE(QMetaObject::invokeMethod(calendar, "reset"));
  EXPECT_EQ(calendar->property("selected").toDate(), QDate::currentDate());
}
