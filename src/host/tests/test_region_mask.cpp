// `Region` and a window's `mask` in a real ShellHost, with the patterns Caelestia's drawers use:
// nested Regions, `item` regions, and a region list from an Instantiator. The fake cursor stands
// in for the real one; nothing here touches the desktop.
#include <gtest/gtest.h>

#include "host/testing/shell_fixtures.h"

namespace {

QString maskShell(const QString& imports, const QString& body) {
  return QStringLiteral(R"(
import QtQuick
import QtQml
%1
ShellRoot {
    PanelWindow {
        id: win
        objectName: "panel"
        anchors { top: true; bottom: true; left: true; right: true }
        color: "transparent"
        Item { id: box; objectName: "box"; x: 500; y: 10; width: 60; height: 30 }
        %2
    }
}
)")
      .arg(imports, body);
}

// The cursor at `x, y` in window coordinates, in the fake physical pixels.
void cursorAt(Harness& h, int x, int y) {
  h.native.cursor = h.window()->geometry().topLeft() + QPoint(x, y);
}

bool clickThrough(Harness& h) {
  return h.window()->flags().testFlag(Qt::WindowTransparentForInput);
}

}  // namespace

TEST(RegionMaskTest, WithoutAMaskTheWholeWindowTakesInput) {
  Harness h;
  h.start(maskShell(QStringLiteral("import wabar"), QString()));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  QsWindow* window = h.shellWindow();
  EXPECT_FALSE(window->maskNode().has_value());
  EXPECT_EQ(window->maskPoller(), nullptr);
  EXPECT_FALSE(clickThrough(h));
}

TEST(RegionMaskTest, AnEmptyRegionMakesTheWindowFullyClickThrough) {
  Harness h;
  h.start(maskShell(QStringLiteral("import wabar"), QStringLiteral("mask: Region {}")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  QsWindow* window = h.shellWindow();
  ASSERT_TRUE(window->maskNode().has_value());
  ASSERT_NE(window->maskPoller(), nullptr);
  for (const QPoint p : {QPoint(0, 0), QPoint(100, 100), QPoint(300, 200)}) {
    cursorAt(h, p.x(), p.y());
    window->maskPoller()->poll();
    EXPECT_TRUE(clickThrough(h));
  }
}

TEST(RegionMaskTest, ACaelestiaStyleMaskLoadsAndEvaluates) {
  Harness h;
  h.start(maskShell(QStringLiteral("import Quickshell"), QStringLiteral(R"(
    property var panels: [ {x: 100, y: 100, w: 50, h: 50}, {x: 300, y: 200, w: 40, h: 40} ]
    Variants {
        id: inst
        model: win.panels
        delegate: Region {
            required property var modelData
            x: modelData.x; y: modelData.y; width: modelData.w; height: modelData.h
            intersection: Intersection.Subtract
        }
    }
    mask: Region {
        x: 20; y: 10; width: win.width - 40; height: win.height - 20
        intersection: Intersection.Xor
        regions: inst.instances
    }
)")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  QsWindow* window = h.shellWindow();
  const std::optional<RegionNode> mask = window->maskNode();
  ASSERT_TRUE(mask.has_value());
  EXPECT_EQ(mask->children.size(), 2);
  EXPECT_TRUE(regionContains(*mask, {60, 60}));
  EXPECT_FALSE(regionContains(*mask, {120, 120}));  // inside the first panel
  EXPECT_FALSE(regionContains(*mask, {320, 220}));  // inside the second
  EXPECT_FALSE(regionContains(*mask, {5, 5}));      // outside the root rectangle

  // The live decision, with the poll driven by hand.
  auto* poller = window->maskPoller();
  ASSERT_NE(poller, nullptr);
  EXPECT_TRUE(poller->running());
  cursorAt(h, 60, 60);
  EXPECT_FALSE(poller->poll());
  EXPECT_FALSE(clickThrough(h));
  cursorAt(h, 125, 125);
  EXPECT_TRUE(poller->poll());
  EXPECT_TRUE(clickThrough(h));
}

TEST(RegionMaskTest, NestedAndItemRegionsFollowTheItem) {
  Harness h;
  h.start(maskShell(QStringLiteral("import wabar"), QStringLiteral(R"(
    mask: Region {
        Region { x: 0; y: 100; width: 100; height: 100 }
        Region { item: box }
        Region { x: 0; y: 0; width: 20; height: 20; intersection: Intersection.Subtract }
    }
)")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  QsWindow* window = h.shellWindow();
  std::optional<RegionNode> mask = window->maskNode();
  ASSERT_TRUE(mask.has_value());
  EXPECT_TRUE(regionContains(*mask, {50, 150}));
  EXPECT_TRUE(regionContains(*mask, {530, 25}));
  EXPECT_FALSE(regionContains(*mask, {10, 10}));
  EXPECT_FALSE(regionContains(*mask, {600, 25}));

  // The item moves; the mask follows without any change to the Region.
  QQuickItem* box = findItem(h.window(), QStringLiteral("box"));
  ASSERT_NE(box, nullptr);
  box->setX(300);
  mask = window->maskNode();
  EXPECT_FALSE(regionContains(*mask, {530, 25}));
  EXPECT_TRUE(regionContains(*mask, {330, 25}));
}

TEST(RegionMaskTest, AnItemRegionIgnoresItsOwnRectangle) {
  Harness h;
  h.start(
      maskShell(QStringLiteral("import wabar"),
                QStringLiteral("mask: Region { x: 0; y: 300; width: 50; height: 50; item: box }")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  const std::optional<RegionNode> mask = h.shellWindow()->maskNode();
  ASSERT_TRUE(mask.has_value());
  EXPECT_TRUE(regionContains(*mask, {510, 20}));
  EXPECT_FALSE(regionContains(*mask, {10, 310}));
}

TEST(RegionMaskTest, HidingTheWindowStopsThePollAndShowingStartsIt) {
  Harness h;
  h.start(maskShell(QStringLiteral("import wabar"),
                    QStringLiteral("mask: Region { x: 0; y: 0; width: 50; height: 50 }")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  QsWindow* window = h.shellWindow();
  ASSERT_NE(window->maskPoller(), nullptr);
  EXPECT_TRUE(window->maskPoller()->running());
  window->setVisible(false);
  EXPECT_FALSE(window->maskPoller()->running());
  window->setVisible(true);
  EXPECT_TRUE(window->maskPoller()->running());
}

TEST(RegionMaskTest, ClearingTheMaskRestoresInput) {
  Harness h;
  h.start(maskShell(QStringLiteral("import wabar"),
                    QStringLiteral("mask: Region { x: 0; y: 0; width: 50; height: 50 }")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  QsWindow* window = h.shellWindow();
  cursorAt(h, 400, 400);
  window->maskPoller()->poll();
  ASSERT_TRUE(clickThrough(h));
  window->setMask(nullptr);
  EXPECT_FALSE(window->maskPoller()->running());
  EXPECT_FALSE(clickThrough(h));
}

TEST(RegionMaskTest, WithoutACursorHookMasksAreInert) {
  // What a headless host (offscreen, no live desktop) looks like: no cursor source.
  Harness h;
  h.native.cursorHook = false;
  h.start(maskShell(QStringLiteral("import wabar"), QStringLiteral("mask: Region {}")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  EXPECT_NE(h.shellWindow()->maskNode(), std::nullopt);
  EXPECT_EQ(h.shellWindow()->maskPoller(), nullptr);
  EXPECT_FALSE(clickThrough(h));
}

TEST(RegionMaskTest, ARealPollTickFlipsTheWindow) {
  Harness h;
  h.start(maskShell(QStringLiteral("import wabar"),
                    QStringLiteral("mask: Region { x: 0; y: 0; width: 50; height: 50 }")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  cursorAt(h, 400, 400);
  QTest::qWait(60);  // several 8 ms ticks
  EXPECT_TRUE(clickThrough(h));
  cursorAt(h, 10, 10);
  QTest::qWait(60);
  EXPECT_FALSE(clickThrough(h));
}
