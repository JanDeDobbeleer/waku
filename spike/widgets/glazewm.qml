// Workspaces of this bar's monitor in GlazeWM, plus the active binding mode. Hidden while
// GlazeWM is not running.
import QtQuick
import QtQuick.Window
import Yasb.Services

Row {
    id: root

    readonly property var monitor: {
        const handle = Window.window ? Window.window.monitorHandle : 0
        for (const m of GlazeWM.monitors)
            if (m.hwnd === handle) return m
        return null
    }

    visible: GlazeWM.connected && monitor !== null
    spacing: 4

    Repeater {
        model: root.monitor ? root.monitor.workspaces : []
        delegate: Rectangle {
            width: 26
            height: 24
            radius: 12
            color: modelData.focused ? "#885b8def" : modelData.hasWindows ? "#44ffffff" : "#18ffffff"
            Behavior on color { ColorAnimation { duration: 150 } }

            Text {
                anchors.centerIn: parent
                color: "white"
                font.pixelSize: 12
                text: modelData.displayName
            }
            TapHandler { onTapped: GlazeWM.activateWorkspace(modelData.name) }
        }
    }

    Text {
        visible: GlazeWM.bindingMode !== ""
        anchors.verticalCenter: parent.verticalCenter
        color: "#f0c36d"
        font.pixelSize: 12
        text: GlazeWM.bindingMode
    }
}
