// `import Quickshell` and `import Quickshell.Wayland`: the Quickshell names of wabar's shell types,
// as a ported shell uses them.
#include <gtest/gtest.h>

#include "host/qml/panel_window.h"
#include "host/testing/shell_fixtures.h"

TEST(QuickshellModule, APortedShellLoads) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import Quickshell
import Quickshell.Wayland
import wabar as W

ShellRoot {
    Scope {
        Variants {
            model: W.ScreenList.screens
            PanelWindow {
                id: win
                required property ShellScreen modelData
                screen: modelData
                anchors { top: true; left: true; right: true }
                implicitHeight: 28
                exclusionMode: ExclusionMode.Ignore
                color: "transparent"
                WlrLayershell.layer: WlrLayer.Overlay
                WlrLayershell.keyboardFocus: WlrKeyboardFocus.None
                WlrLayershell.namespace: "caelestia-bar"

                Item {
                    id: anchorItem
                    objectName: "anchorItem"
                    width: 40; height: 20
                    readonly property bool inWindow: QsWindow.window === win
                    PopupWindow {
                        objectName: "qsPopup"
                        anchor.item: anchorItem
                        anchor.edges: Edges.Bottom
                    }
                }
            }
        }
    }
    LazyLoader { objectName: "lazy"; FloatingWindow { title: "later" } }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  ASSERT_EQ(h.host->windows().size(), 1);
  auto* panel = qobject_cast<PanelWindow*>(h.shellWindow());
  ASSERT_NE(panel, nullptr);
  EXPECT_EQ(panel->layer(), WlrLayer::Overlay);
  EXPECT_EQ(panel->layerNamespace(), QStringLiteral("caelestia-bar"));
  EXPECT_EQ(panel->exclusionMode(), ExclusionMode::Ignore);
  QQuickItem* item = findItem(h.window(), QStringLiteral("anchorItem"));
  ASSERT_NE(item, nullptr);
  EXPECT_TRUE(item->property("inWindow").toBool());

  QQuickWindow* popup = findWindow(QStringLiteral("qsPopup"));
  ASSERT_NE(popup, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(popup, "open"));
  EXPECT_TRUE(popup->isVisible());
  EXPECT_GE(popup->y(), h.window()->y() + h.window()->height());  // Edges.Bottom: below
}

TEST(QuickshellModule, WabarAndQuickshellNamesAreTheSameTypes) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(QByteArrayLiteral(R"(
import QtQml
import Quickshell
import wabar
ShellRoot {}
)"),
                    QUrl(QStringLiteral("file:///both.qml")));
  ASSERT_FALSE(component.isError()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> root(component.create());
  EXPECT_NE(qobject_cast<ShellRoot*>(root.get()), nullptr);
}

TEST(QuickshellModule, TheQuickshellSingletonHasScreensEnvAndShellDir) {
  Harness h;
  h.screenCount = 2;
  h.start(QStringLiteral(R"(
import QtQuick
import Quickshell

ShellRoot {
    QtObject {
        objectName: "probe"
        readonly property int screenCount: Quickshell.screens.length
        readonly property var path: Quickshell.env("PATH")
        readonly property var missing: Quickshell.env("WABAR_SURELY_NOT_SET_ANYWHERE")
        readonly property string dir: Quickshell.shellDir
        readonly property string statePath: Quickshell.statePath("x.json")
        readonly property bool hasIcon: Quickshell.hasThemeIcon("no-such-icon-anywhere")
        readonly property real pid: Quickshell.processId
    }
    Variants {
        model: Quickshell.screens
        PanelWindow {
            required property ShellScreen modelData
            screen: modelData
            anchors.top: true
            implicitHeight: 20
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  auto* probe = h.host->shell()->findChild<QObject*>(QStringLiteral("probe"));
  ASSERT_NE(probe, nullptr);
  EXPECT_EQ(probe->property("screenCount").toInt(), 2);
  EXPECT_FALSE(probe->property("path").toString().isEmpty());
  EXPECT_TRUE(probe->property("missing").isNull());
  EXPECT_FALSE(probe->property("dir").toString().isEmpty());
  EXPECT_TRUE(probe->property("statePath").toString().endsWith(QStringLiteral("x.json")));
  EXPECT_FALSE(probe->property("hasIcon").toBool());
  EXPECT_GT(probe->property("pid").toDouble(), 0);
  EXPECT_EQ(h.host->windows().size(), 2);  // Variants over Quickshell.screens
}

TEST(QuickshellModule, APopupWindowWithVisibleTrueBoundPlacesItselfNextToItsAnchor) {
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import Quickshell

ShellRoot {
    PanelWindow {
        anchors { top: true; left: true; right: true }
        implicitHeight: 28
        Item {
            id: anchorItem
            x: 200
            width: 40
            height: 20
            PopupWindow {
                objectName: "qsPopup"
                anchor.item: anchorItem
                anchor.edges: Edges.Bottom
                visible: true
                Text { text: "tip" }
            }
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  QQuickWindow* popup = findWindow(QStringLiteral("qsPopup"));
  ASSERT_NE(popup, nullptr);
  // A window inside an item opens when the window of that item does.
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->isVisible(); }, 3000));
  settle();
  // Below the panel and over the anchor, not in the screen corner a window starts in.
  EXPECT_GE(popup->y(), h.window()->y() + h.window()->height());
  EXPECT_GT(popup->x(), 0);
}

// `import Quickshell` exposes Quickshell's own names only: a shell's local Wallpapers.qml,
// Workspaces.qml and Bluetooth.qml must not lose to wabar's services of the same names.
TEST(QuickshellModule, LocalTypesAreNotShadowedByWabarServices) {
  Harness h;
  ASSERT_TRUE(h.dir.isValid());
  for (const char* name : {"Wallpapers", "Workspaces", "Bluetooth", "Media", "Notifications"}) {
    write(h.dir.filePath(QStringLiteral("%1.qml").arg(QLatin1String(name))),
          QStringLiteral("import QtQml\nQtObject { property string kind: \"%1\" }\n")
              .arg(QLatin1String(name)));
  }
  h.start(QStringLiteral(R"(
import Quickshell
import Quickshell.Io
import Quickshell.Services.Mpris
import Quickshell.Services.Notifications

ShellRoot {
    Wallpapers { id: wallpapers }
    Workspaces { id: workspaces }
    Bluetooth { id: bluetooth }
    Media { id: media }
    Notifications { id: notifications }
    Scope {
        objectName: "probe"
        property string kinds: [wallpapers, workspaces, bluetooth, media, notifications]
            .map(o => o.kind).join(",")
    }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  const auto* probe = h.host->shell()->findChild<QObject*>(QStringLiteral("probe"));
  ASSERT_NE(probe, nullptr);
  EXPECT_EQ(probe->property("kinds").toString(),
            QStringLiteral("Wallpapers,Workspaces,Bluetooth,Media,Notifications"));
}

TEST(QuickshellModule, TheQuickshellNamesStillResolve) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(QByteArrayLiteral(R"(
import QtQml
import Quickshell
import Quickshell.Io
import Quickshell.Services.Mpris
import Quickshell.Services.Notifications
QtObject {
    property var entries: DesktopEntries
    property var proc: Process { command: ["x"] }
    property var parser: StdioCollector {}
    property var view: FileView {}
    property var json: JsonAdapter {}
    property int err: FileViewError.FileNotFound
    property var mpris: Mpris.players
    property int playing: MprisPlaybackState.Playing
    property int loop: MprisLoopState.Track
    property var server: NotificationServer {}
    property int urgency: NotificationUrgency.Critical
    property int reason: NotificationCloseReason.Dismissed
}
)"),
                    QUrl(QStringLiteral("file:///names.qml")));
  ASSERT_FALSE(component.isError()) << qPrintable(component.errorString());
  std::unique_ptr<QObject> root(component.create());
  ASSERT_NE(root, nullptr) << qPrintable(component.errorString());
  EXPECT_EQ(root->property("playing").toInt(), 1);
  EXPECT_EQ(root->property("urgency").toInt(), 2);
}

TEST(QuickshellModule, APopupWindowIsBareAndSizedByImplicitWidthAndHeight) {
  // Quickshell's PopupWindow draws nothing and lays nothing out: a child with `anchors.fill:
  // parent` fills the window, which is as big as implicitWidth and implicitHeight say.
  Harness h;
  h.start(QStringLiteral(R"(
import QtQuick
import Quickshell

ShellRoot {
    PanelWindow {
        anchors { top: true; left: true; right: true }
        implicitHeight: 28
        Item {
            id: anchorItem
            x: 200
            width: 40
            height: 20
            PopupWindow {
                objectName: "qsPopup"
                anchor.item: anchorItem
                implicitWidth: 140
                implicitHeight: 70
                visible: true
                Rectangle { objectName: "fill"; anchors.fill: parent; color: "red" }
            }
        }
    }
}
)"));
  ASSERT_TRUE(h.host->reload()) << qPrintable(h.host->error());
  QQuickWindow* popup = findWindow(QStringLiteral("qsPopup"));
  ASSERT_NE(popup, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->isVisible(); }, 3000));
  settle();
  EXPECT_EQ(popup->width(), 140);
  EXPECT_EQ(popup->height(), 70);
  QQuickItem* fill = findItem(popup->contentItem(), QStringLiteral("fill"));
  ASSERT_NE(fill, nullptr);
  EXPECT_EQ(fill->width(), 140);
  EXPECT_EQ(fill->height(), 70);
  // No themed surface behind it.
  EXPECT_EQ(findItem(popup->contentItem(), QStringLiteral("popupSurface")), nullptr);
}
