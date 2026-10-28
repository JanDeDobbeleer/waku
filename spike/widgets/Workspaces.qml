import QtQuick
import QtQuick.Window
import barnacle.services as S

// The workspaces of this bar's monitor, from whichever tiling window manager is running (komorebi
// or GlazeWM). A click shows a workspace, the wheel steps through them. Hidden when neither runs.
//
//     Workspaces { hideEmpty: true; scrollable: false }
//
// `hideEmpty` hides workspaces that are neither shown nor hold windows.
Row {
    id: root

    property int pillWidth: 26
    property bool hideEmpty: false
    property bool scrollable: true
    property color focusedFill: "#885b8def"
    property color activeFill: "#55ffffff"
    property color usedFill: "#33ffffff"
    property color emptyFill: "#18ffffff"

    // HMONITOR of the bar this widget is in (set on the bar's window by the host).
    readonly property real handle: Window.window ? Window.window.monitorHandle : 0
    readonly property var monitor: {
        for (const m of S.Workspaces.monitors)
            if (m.handle === handle) return m;
        return null;
    }

    visible: S.Workspaces.connected && monitor !== null
    spacing: 4

    Repeater {
        model: root.monitor ? root.monitor.workspaces : []

        delegate: Rectangle {
            required property var modelData

            visible: !root.hideEmpty || modelData.active || modelData.hasWindows
            width: root.pillWidth
            height: 24
            radius: height / 2
            color: modelData.focused ? root.focusedFill
                   : modelData.active ? root.activeFill
                   : modelData.hasWindows ? root.usedFill : root.emptyFill

            Behavior on color {
                ColorAnimation {
                    duration: 150
                }
            }

            Text {
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 12
                text: modelData.label
            }

            TapHandler {
                onTapped: S.Workspaces.focus(root.handle, modelData.id)
            }
        }
    }

    WheelHandler {
        enabled: root.scrollable
        onWheel: event => event.angleDelta.y > 0 ? S.Workspaces.focusPrevious(root.handle)
                                                  : S.Workspaces.focusNext(root.handle)
    }
}
