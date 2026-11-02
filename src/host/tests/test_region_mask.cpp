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
  h.start(maskShell(QStringLiteral("import waku"), QString()));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  QsWindow* window = h.shellWindow();
  EXPECT_FALSE(window->maskNode().has_value());
  EXPECT_EQ(window->maskPoller(), nullptr);
  EXPECT_FALSE(clickThrough(h));
}

TEST(RegionMaskTest, AnEmptyRegionMakesTheWindowFullyClickThrough) {
  Harness h;
  h.start(maskShell(QStringLiteral("import waku"), QStringLiteral("mask: Region {}")));
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
  const QSizeF size = h.window()->size();
  // The root Xor: input is the window minus (inner area minus the open panels).
  EXPECT_FALSE(regionContainsInWindow(*mask, {60, 60}, size));   // the inner area
  EXPECT_TRUE(regionContainsInWindow(*mask, {120, 120}, size));  // inside the first panel
  EXPECT_TRUE(regionContainsInWindow(*mask, {320, 220}, size));  // inside the second
  EXPECT_TRUE(regionContainsInWindow(*mask, {5, 5}, size));      // the border

  // The live decision, with the poll driven by hand.
  auto* poller = window->maskPoller();
  ASSERT_NE(poller, nullptr);
  EXPECT_TRUE(poller->running());
  cursorAt(h, 125, 125);
  EXPECT_FALSE(poller->poll());
  EXPECT_FALSE(clickThrough(h));
  cursorAt(h, 60, 60);
  EXPECT_TRUE(poller->poll());
  EXPECT_TRUE(clickThrough(h));
  cursorAt(h, 3, 3);
  EXPECT_FALSE(poller->poll());
}

TEST(RegionMaskTest, NestedAndItemRegionsFollowTheItem) {
  Harness h;
  h.start(maskShell(QStringLiteral("import waku"), QStringLiteral(R"(
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
      maskShell(QStringLiteral("import waku"),
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
  h.start(maskShell(QStringLiteral("import waku"),
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
  h.start(maskShell(QStringLiteral("import waku"),
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
  h.start(maskShell(QStringLiteral("import waku"), QStringLiteral("mask: Region {}")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  EXPECT_NE(h.shellWindow()->maskNode(), std::nullopt);
  EXPECT_EQ(h.shellWindow()->maskPoller(), nullptr);
  EXPECT_FALSE(clickThrough(h));
}

TEST(RegionMaskTest, ARealPollTickFlipsTheWindow) {
  Harness h;
  h.start(maskShell(QStringLiteral("import waku"),
                    QStringLiteral("mask: Region { x: 0; y: 0; width: 50; height: 50 }")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  // The fake cursor jumps (it starts far off screen), so the next tick may be a slow one.
  cursorAt(h, 400, 400);
  EXPECT_TRUE(QTest::qWaitFor([&] { return clickThrough(h); }, 1000));
  cursorAt(h, 10, 10);
  EXPECT_TRUE(QTest::qWaitFor([&] { return !clickThrough(h); }, 1000));
}

TEST(RegionMaskTest, AChangedRegionIsTestedAgainUnderARestingCursor) {
  Harness h;
  h.start(maskShell(QStringLiteral("import waku"), QStringLiteral(R"(
    property alias inner: inner
    mask: Region { x: 0; y: 0; width: 50; height: 50; Region { id: inner; x: 0; y: 0; width: 1; height: 1 } }
)")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  ASSERT_NE(h.host->maskHub(), nullptr);
  EXPECT_EQ(h.host->maskHub()->pollerCount(), 1);
  cursorAt(h, 200, 200);
  h.host->maskHub()->tick();
  ASSERT_TRUE(clickThrough(h));
  const int evaluations = h.host->maskHub()->evaluations();
  h.host->maskHub()->tick();  // nothing changed, the cursor rests
  EXPECT_EQ(h.host->maskHub()->evaluations(), evaluations);
  // A nested region grows over the cursor: the change reaches the root and the poller.
  QObject* inner = h.shellWindow()->property("inner").value<QObject*>();
  ASSERT_NE(inner, nullptr);
  inner->setProperty("width", 400);
  inner->setProperty("height", 400);
  h.host->maskHub()->tick();
  EXPECT_FALSE(clickThrough(h));
}

namespace {

QStringList* g_warnings = nullptr;
QtMessageHandler g_previousHandler = nullptr;

void collectWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  if (g_warnings && type >= QtWarningMsg) g_warnings->append(message);
  if (g_previousHandler) g_previousHandler(type, context, message);
}

// Collects warnings while alive.
struct WarningCapture {
  WarningCapture() {
    g_warnings = &messages;
    g_previousHandler = qInstallMessageHandler(collectWarnings);
  }
  ~WarningCapture() {
    qInstallMessageHandler(g_previousHandler);
    g_warnings = nullptr;
  }
  QStringList messages;
};

}  // namespace

TEST(RegionMaskTest, EachRegionPropertyHasItsOwnNotifySignal) {
  Region region;
  const QMetaObject* meta = region.metaObject();
  for (const char* name : {"x", "y", "width", "height", "shape", "item", "intersection"}) {
    const QMetaProperty property = meta->property(meta->indexOfProperty(name));
    ASSERT_TRUE(property.isValid()) << name;
    EXPECT_EQ(property.notifySignal().name(), QByteArray(name) + "Changed") << name;
  }
}

TEST(RegionMaskTest, ABindingThatReadsAnotherRegionPropertyDoesNotLoop) {
  WarningCapture capture;
  Harness h;
  h.start(maskShell(QStringLiteral("import waku"), QStringLiteral(R"(
    property int w: 120
    property int h: 80
    mask: Region {
        id: region
        x: region.width / 4
        y: region.height / 4
        width: win.w
        height: win.h
    }
)")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  for (const QString& message : capture.messages) {
    EXPECT_FALSE(message.contains(QStringLiteral("inding loop"))) << qPrintable(message);
  }
  QsWindow* window = h.shellWindow();
  std::optional<RegionNode> mask = window->maskNode();
  ASSERT_TRUE(mask.has_value());
  EXPECT_EQ(mask->rect, QRectF(30, 20, 120, 80));

  // The mask still follows every property.
  window->setProperty("w", 200);
  window->setProperty("h", 40);
  settle();
  mask = window->maskNode();
  ASSERT_TRUE(mask.has_value());
  EXPECT_EQ(mask->rect, QRectF(50, 10, 200, 40));
  for (const QString& message : capture.messages) {
    EXPECT_FALSE(message.contains(QStringLiteral("inding loop"))) << qPrintable(message);
  }
}

TEST(RegionMaskTest, EveryPropertyChangeReachesTheMaskPoller) {
  Harness h;
  h.start(maskShell(QStringLiteral("import waku"), QStringLiteral(R"(
    property alias inner: inner
    mask: Region { id: inner; x: 0; y: 0; width: 50; height: 50 }
)")));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  settle();
  QObject* inner = h.shellWindow()->property("inner").value<QObject*>();
  ASSERT_NE(inner, nullptr);
  cursorAt(h, 200, 200);
  h.host->maskHub()->tick();
  ASSERT_TRUE(clickThrough(h));
  int evaluations = h.host->maskHub()->evaluations();
  const std::pair<const char*, int> changes[] = {
      {"x", 150}, {"y", 150}, {"width", 100}, {"height", 100}};
  for (const auto& [name, value] : changes) {
    inner->setProperty(name, value);
    h.host->maskHub()->tick();
    EXPECT_GT(h.host->maskHub()->evaluations(), evaluations) << name;
    evaluations = h.host->maskHub()->evaluations();
  }
  EXPECT_FALSE(clickThrough(h));
}
