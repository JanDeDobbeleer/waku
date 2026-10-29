// The Cpu, Memory and Battery widgets in a real bar on the offscreen platform; they sample the
// real machine, so the assertions are about shape, not values.
#include <gtest/gtest.h>

#include <QRegularExpression>

#include "host/testing/widget_fixtures.h"

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
