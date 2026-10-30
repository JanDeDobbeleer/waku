// `import Quickshell` and `import Quickshell.Wayland`: the Quickshell names of wabar's shell types,
// as a ported shell uses them.
#include <gtest/gtest.h>

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
