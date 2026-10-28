// Workspaces of this bar's monitor, from whichever tiling window manager is running (komorebi or
// GlazeWM). Click a workspace to show it, scroll to step through them. Hidden when neither runs.
import QtQuick
import QtQuick.Window
import barnacle.services

Row {
    id: root

    readonly property real handle: Window.window ? Window.window.monitorHandle : 0
    readonly property var monitor: {
        for (const m of Workspaces.monitors)
            if (m.handle === handle) return m
        return null
    }

    visible: Workspaces.connected && monitor !== null
    spacing: 4

    Repeater {
        model: root.monitor ? root.monitor.workspaces : []
        delegate: Rectangle {
            width: 26
            height: 24
            radius: 12
            color: modelData.focused ? "#885b8def"
                 : modelData.active ? "#55ffffff"
                 : modelData.hasWindows ? "#33ffffff" : "#18ffffff"
            Behavior on color { ColorAnimation { duration: 150 } }

            Text {
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 12
                text: modelData.label
            }
            TapHandler { onTapped: Workspaces.focus(root.handle, modelData.id) }
        }
    }

    WheelHandler {
        onWheel: event => event.angleDelta.y > 0 ? Workspaces.focusPrevious(root.handle)
                                                  : Workspaces.focusNext(root.handle)
    }
}
