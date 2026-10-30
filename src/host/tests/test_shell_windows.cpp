// The window types of a shell (PanelWindow, FloatingWindow), QsWindow, Variants and LazyLoader in a
// real ShellHost on the offscreen platform.
#include <gtest/gtest.h>

#include <QQmlListReference>

#include "host/qml/lazy_loader.h"
#include "host/testing/shell_fixtures.h"

namespace {

QString panelShell(const QString& properties) {
  return QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    PanelWindow {
        objectName: "panel"
        implicitWidth: 120
        implicitHeight: 30
        %1
        Rectangle { objectName: "content"; anchors.fill: parent; color: "red" }
    }
}
)")
      .arg(properties);
}

QRect screenRect() { return QGuiApplication::primaryScreen()->geometry(); }

QRect startPanel(Harness& h, const QString& properties) {
  h.start(panelShell(properties));
  if (!h.host->reload() || !h.window()) return {};
  settle();
  return h.window()->geometry();
}

}  // namespace

// ---- PanelWindow geometry for each anchor combination ----

TEST(ShellWindowsTest, APanelAnchoredTopLeftRightSpansTheTop) {
  Harness h;
  const QRect s = screenRect();
  EXPECT_EQ(startPanel(h, QStringLiteral("anchors { top: true; left: true; right: true }")),
            QRect(s.x(), s.y(), s.width(), 30));
}

TEST(ShellWindowsTest, APanelAnchoredBottomSitsOnTheBottomEdge) {
  Harness h;
  const QRect s = screenRect();
  EXPECT_EQ(startPanel(h, QStringLiteral("anchors { bottom: true; left: true; right: true }")),
            QRect(s.x(), s.y() + s.height() - 30, s.width(), 30));
}

TEST(ShellWindowsTest, PanelsAnchoredLeftOrRightSpanTheSide) {
  const QRect s = screenRect();
  Harness left;
  EXPECT_EQ(startPanel(left, QStringLiteral("anchors { left: true; top: true; bottom: true }")),
            QRect(s.x(), s.y(), 120, s.height()));
  Harness right;
  EXPECT_EQ(startPanel(right, QStringLiteral("anchors { right: true; top: true; bottom: true }")),
            QRect(s.x() + s.width() - 120, s.y(), 120, s.height()));
}

TEST(ShellWindowsTest, AllFourAnchorsCoverTheScreen) {
  Harness h;
  EXPECT_EQ(
      startPanel(h, QStringLiteral("anchors { top: true; bottom: true; left: true; right: true }")),
      screenRect());
  // The content fills it.
  QQuickItem* content = findItem(h.window(), QStringLiteral("content"));
  ASSERT_NE(content, nullptr);
  EXPECT_EQ(content->width(), screenRect().width());
  // A full-screen window is not an app bar.
  EXPECT_TRUE(h.native.created.empty());
}

TEST(ShellWindowsTest, MarginsAndNoAnchorsPlaceTheWindow) {
  const QRect s = screenRect();
  Harness margins;
  EXPECT_EQ(startPanel(margins, QStringLiteral("anchors { top: true; left: true; right: true }\n"
                                               "margins { top: 4; left: 10; right: 20 }")),
            QRect(s.x() + 10, s.y() + 4, s.width() - 30, 30));
  Harness centred;
  EXPECT_EQ(startPanel(centred, QString()),
            QRect(s.x() + (s.width() - 120) / 2, s.y() + (s.height() - 30) / 2, 120, 30));
}

TEST(ShellWindowsTest, GeometryFollowsChangesAfterItIsShown) {
  Harness h;
  startPanel(h, QStringLiteral("anchors { top: true; left: true; right: true }"));
  QsWindow* window = h.shellWindow();
  ASSERT_NE(window, nullptr);
  window->setProperty("implicitHeight", 50);
  EXPECT_EQ(h.window()->height(), 50);
  EXPECT_EQ(window->property("height").toInt(), 50);
}

// ---- layer, focus and the exclusive zone ----

TEST(ShellWindowsTest, LayersAndKeyboardFocusSetTheWindowFlags) {
  Harness top;
  startPanel(top, QStringLiteral("anchors.top: true"));
  EXPECT_TRUE(top.window()->flags().testFlag(Qt::WindowStaysOnTopHint));
  EXPECT_TRUE(top.window()->flags().testFlag(Qt::WindowDoesNotAcceptFocus));

  Harness below;
  startPanel(below, QStringLiteral("anchors.top: true; layer: WlrLayer.Bottom; "
                                   "keyboardFocus: WlrKeyboardFocus.OnDemand"));
  EXPECT_FALSE(below.window()->flags().testFlag(Qt::WindowStaysOnTopHint));
  EXPECT_FALSE(below.window()->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
  auto* panel = qobject_cast<PanelWindow*>(below.shellWindow());
  ASSERT_NE(panel, nullptr);
  EXPECT_FALSE(panel->aboveWindows());
  EXPECT_TRUE(panel->focusable());
  ASSERT_EQ(below.native.created.size(), 1u);
  EXPECT_FALSE(below.native.created[0].request.alwaysOnTop);
  EXPECT_FALSE(below.native.created[0].request.noActivate);
}

TEST(ShellWindowsTest, AboveWindowsAndFocusableAreTheSameState) {
  Harness h;
  startPanel(h, QStringLiteral("anchors.top: true; aboveWindows: false; focusable: true"));
  auto* panel = qobject_cast<PanelWindow*>(h.shellWindow());
  ASSERT_NE(panel, nullptr);
  EXPECT_EQ(panel->layer(), WlrLayer::Bottom);
  EXPECT_EQ(panel->keyboardFocus(), WlrKeyboardFocus::OnDemand);
}

TEST(ShellWindowsTest, SettingTheExclusiveZoneSwitchesAutoToNormal) {
  Harness h;
  h.allowReserve = true;
  startPanel(h,
             QStringLiteral("anchors { top: true; left: true; right: true }\nexclusiveZone: 12"));
  auto* panel = qobject_cast<PanelWindow*>(h.shellWindow());
  ASSERT_NE(panel, nullptr);
  EXPECT_EQ(panel->exclusionMode(), ExclusionMode::Normal);
  ASSERT_EQ(h.native.created.size(), 1u);
  EXPECT_TRUE(h.native.created[0].request.reserveSpace);
  EXPECT_EQ(h.native.created[0].request.height, 12);
}

TEST(ShellWindowsTest, AutoReservesTheThicknessAndIgnoreNothing) {
  Harness autoZone;
  autoZone.allowReserve = true;
  startPanel(autoZone, QStringLiteral("anchors { top: true; left: true; right: true }"));
  ASSERT_EQ(autoZone.native.created.size(), 1u);
  EXPECT_TRUE(autoZone.native.created[0].request.reserveSpace);
  EXPECT_EQ(autoZone.native.created[0].request.height, 30);

  Harness ignore;
  ignore.allowReserve = true;
  startPanel(ignore, QStringLiteral("anchors { top: true; left: true; right: true }\n"
                                    "exclusionMode: ExclusionMode.Ignore"));
  ASSERT_EQ(ignore.native.created.size(), 1u);
  EXPECT_FALSE(ignore.native.created[0].request.reserveSpace);
}

TEST(ShellWindowsTest, WlrLayershellAttachedPropertiesWriteThePanelState) {
  Harness h;
  startPanel(h, QStringLiteral("anchors.top: true\n"
                               "WlrLayershell.layer: WlrLayer.Overlay\n"
                               "WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand\n"
                               "WlrLayershell.namespace: \"wabar-test\"\n"
                               "WlrLayershell.exclusionMode: ExclusionMode.Ignore"));
  auto* panel = qobject_cast<PanelWindow*>(h.shellWindow());
  ASSERT_NE(panel, nullptr);
  EXPECT_EQ(panel->layer(), WlrLayer::Overlay);
  EXPECT_EQ(panel->keyboardFocus(), WlrKeyboardFocus::OnDemand);
  EXPECT_EQ(panel->layerNamespace(), QStringLiteral("wabar-test"));
  EXPECT_EQ(panel->exclusionMode(), ExclusionMode::Ignore);
}

TEST(ShellWindowsTest, AHiddenPanelShowsWhenMadeVisible) {
  Harness h;
  h.start(panelShell(QStringLiteral("anchors.top: true; visible: false")));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->windows().isEmpty());
  EXPECT_TRUE(h.native.backdrops.empty());  // no native look before the first show
  auto* panel = h.host->shell()->findChild<PanelWindow*>(QStringLiteral("panel"));
  ASSERT_NE(panel, nullptr);
  panel->setVisible(true);
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.native.backdrops.size(), 1u);
}

// ---- QsWindow attached ----

TEST(ShellWindowsTest, QsWindowAttachedFindsTheWindowOfAnItem) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    PanelWindow {
        objectName: "panel"
        anchors { top: true; left: true; right: true }
        implicitHeight: 30
        Item {
            id: box
            objectName: "box"
            x: 40; y: 5; width: 10; height: 10
            readonly property var win: QsWindow.window
            readonly property var content: QsWindow.contentItem
            readonly property point at: QsWindow.window ? QsWindow.window.itemPosition(box) : Qt.point(-1, -1)
            readonly property point mapped: QsWindow.window ? QsWindow.window.mapFromItem(box, 2, 3) : Qt.point(-1, -1)
            readonly property rect rect: QsWindow.window ? QsWindow.window.itemRect(box) : Qt.rect(0, 0, 0, 0)
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  QQuickItem* box = findItem(h.window(), QStringLiteral("box"));
  ASSERT_NE(box, nullptr);
  EXPECT_EQ(box->property("win").value<QObject*>(), h.shellWindow());
  EXPECT_EQ(box->property("content").value<QObject*>(), h.window()->contentItem());
  EXPECT_EQ(box->property("at").toPointF(), QPointF(40, 5));
  EXPECT_EQ(box->property("mapped").toPointF(), QPointF(42, 8));
  EXPECT_EQ(box->property("rect").toRectF(), QRectF(40, 5, 10, 10));
}

// ---- Variants and ScreenList ----

TEST(ShellWindowsTest, VariantsBuildsAWindowPerScreen) {
  Harness h;
  h.screenCount = 3;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    Variants {
        objectName: "variants"
        model: ScreenList.screens
        PanelWindow {
            required property ShellScreen modelData
            screen: modelData
            anchors { top: true; left: true; right: true }
            implicitHeight: 20
            Text { objectName: "name"; text: modelData.name + "|" + modelData.width }
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.host->windows().size(), 3);
  const QScreen* screen = QGuiApplication::primaryScreen();
  EXPECT_EQ(h.text(QStringLiteral("name")),
            screen->name() + QStringLiteral("|") + QString::number(screen->geometry().width()));
  QObject* variants = h.host->shell()->findChild<QObject*>(QStringLiteral("variants"));
  ASSERT_NE(variants, nullptr);
  EXPECT_EQ(QQmlListReference(variants, "instances").count(), 3);
}

TEST(ShellWindowsTest, ScreenListDescribesTheScreens) {
  Harness h;
  h.screenCount = 2;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    PanelWindow {
        anchors.top: true
        Text {
            objectName: "info"
            text: ScreenList.screens.length + "|" + ScreenList.primary.name + "|"
                  + ScreenList.screens[0].primary + "|" + ScreenList.screens[1].primary + "|"
                  + ScreenList.match(["all"]).length + "|" + ScreenList.match(["primary"]).length
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.text(QStringLiteral("info")),
            QStringLiteral("2|%1|true|false|2|1").arg(QGuiApplication::primaryScreen()->name()));
}

// ---- LazyLoader ----

TEST(ShellWindowsTest, ALazyLoaderBuildsNothingUntilActivated) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    LazyLoader {
        objectName: "loader"
        PanelWindow {
            anchors { top: true; left: true; right: true }
            implicitHeight: 24
            Text { objectName: "lazy"; text: "lazy" }
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  EXPECT_TRUE(h.host->windows().isEmpty());
  auto* loader = h.host->shell()->findChild<LazyLoader*>(QStringLiteral("loader"));
  ASSERT_NE(loader, nullptr);
  EXPECT_FALSE(loader->active());
  EXPECT_EQ(loader->item(), nullptr);

  loader->setActive(true);
  EXPECT_TRUE(loader->active());
  ASSERT_NE(loader->item(), nullptr);
  ASSERT_EQ(h.host->windows().size(), 1);  // shown at once: the shell is installed
  EXPECT_EQ(h.text(QStringLiteral("lazy")), QStringLiteral("lazy"));
  EXPECT_EQ(h.native.created.size(), 1u);

  loader->setActive(false);
  EXPECT_EQ(loader->item(), nullptr);
  EXPECT_TRUE(h.host->windows().isEmpty());
  EXPECT_EQ(h.native.removed, 1);
}

TEST(ShellWindowsTest, ALazyLoaderLoadsInTheBackground) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    LazyLoader {
        objectName: "loader"
        loading: true
        PanelWindow { anchors.top: true; Text { objectName: "lazy"; text: "later" } }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  auto* loader = h.host->shell()->findChild<LazyLoader*>(QStringLiteral("loader"));
  ASSERT_NE(loader, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return loader->active(); }, 3000));
  EXPECT_FALSE(loader->loading());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("lazy")), QStringLiteral("later"));
}

TEST(ShellWindowsTest, ALazyLoaderActiveAsyncStartsABackgroundBuildAndFalseDestroys) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    LazyLoader {
        objectName: "loader"
        activeAsync: true
        PanelWindow { anchors.top: true; Text { objectName: "lazy"; text: "async" } }
    }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  auto* loader = h.host->shell()->findChild<LazyLoader*>(QStringLiteral("loader"));
  ASSERT_NE(loader, nullptr);
  const int index = loader->metaObject()->indexOfProperty("activeAsync");
  ASSERT_GE(index, 0);
  QSignalSpy changed(loader, &LazyLoader::activeAsyncChanged);
  EXPECT_TRUE(loader->property("activeAsync").toBool());  // started, maybe not built yet
  ASSERT_TRUE(QTest::qWaitFor([&] { return loader->active(); }, 3000));
  EXPECT_TRUE(loader->property("activeAsync").toBool());
  ASSERT_EQ(h.host->windows().size(), 1);
  EXPECT_EQ(h.text(QStringLiteral("lazy")), QStringLiteral("async"));

  ASSERT_TRUE(loader->setProperty("activeAsync", false));
  EXPECT_EQ(loader->item(), nullptr);
  EXPECT_FALSE(loader->property("activeAsync").toBool());
  EXPECT_TRUE(h.host->windows().isEmpty());
  EXPECT_GE(changed.count(), 1);

  ASSERT_TRUE(loader->setProperty("activeAsync", true));
  EXPECT_TRUE(loader->loading() || loader->active());
  ASSERT_TRUE(QTest::qWaitFor([&] { return loader->active(); }, 3000));
}

TEST(ShellWindowsTest, AnActiveLazyLoaderBuildsWithTheShell) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    LazyLoader { active: true; PanelWindow { anchors.top: true } }
}
)"));
  ASSERT_TRUE(h.host->reload());
  EXPECT_EQ(h.host->windows().size(), 1);
}

// ---- FloatingWindow ----

TEST(ShellWindowsTest, AFloatingWindowIsANormalWindowAtItsSize) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    FloatingWindow {
        title: "hello"
        implicitWidth: 300
        implicitHeight: 200
        minimumSize: Qt.size(100, 80)
        Text { objectName: "inside"; text: "x" }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  ASSERT_EQ(h.host->windows().size(), 1);
  QQuickWindow* window = h.window();
  EXPECT_EQ(window->title(), QStringLiteral("hello"));
  EXPECT_EQ(window->size(), QSize(300, 200));
  EXPECT_EQ(window->minimumSize(), QSize(100, 80));
  EXPECT_FALSE(window->flags().testFlag(Qt::FramelessWindowHint));
  EXPECT_TRUE(h.native.created.empty());  // never an app bar
  EXPECT_NE(findItem(window, QStringLiteral("inside")), nullptr);
}

// ---- parts ----

TEST(ShellWindowsTest, PartsInScopesAreActivatedOnceTheShellIsInstalled) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import wabar
ShellRoot {
    QtObject { objectName: "top"; property int started: 0; function activate() { started++ } }
    Scope {
        QtObject { objectName: "nested"; property int started: 0; function activate() { started++ } }
    }
}
)"));
  ASSERT_TRUE(h.host->reload());
  QObject* top = h.host->shell()->findChild<QObject*>(QStringLiteral("top"));
  QObject* nested = h.host->shell()->findChild<QObject*>(QStringLiteral("nested"));
  ASSERT_NE(top, nullptr);
  ASSERT_NE(nested, nullptr);
  EXPECT_EQ(top->property("started").toInt(), 1);
  EXPECT_EQ(nested->property("started").toInt(), 1);
}
