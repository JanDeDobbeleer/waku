// Workspaces of this bar's monitor in komorebi. Hidden while komorebi is not running.
import QtQuick
import QtQuick.Window
import barnacle.services

Row {
    id: root

    readonly property var monitor: {
        const handle = Window.window ? Window.window.monitorHandle : 0
        for (const m of Komorebi.monitors)
            if (m.id === handle) return m
        return null
    }

    visible: Komorebi.connected && monitor !== null
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
                text: modelData.name !== "" ? modelData.name : modelData.index + 1
            }
            TapHandler { onTapped: Komorebi.focusWorkspace(root.monitor.index, modelData.index) }
        }
    }
}
