import QtQuick
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: bar

    property int barHeight: 40
    // HMONITOR of the screen this bar is on, set by the host.
    property var monitorHandle: 0

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"
    x: screen.virtualX
    y: screen.virtualY
    width: screen.width
    height: barHeight
    visible: false

    Rectangle {
        anchors.fill: parent
        color: "#66101018"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 0

        Clock {}

        Item { Layout.fillWidth: true }

        // User widgets: every .qml file in the widgets folder, reloaded when it changes.
        Repeater {
            model: Host.widgetFiles
            delegate: Loader {
                source: modelData
                // A widget that hides itself (`visible: false`) also leaves the layout. The
                // Loader itself stays visible, so item.visible is the widget's own setting.
                readonly property bool shown: status === Loader.Ready && item.visible
                Layout.preferredWidth: shown ? item.implicitWidth : 0
                Layout.leftMargin: shown ? 12 : 0
                onStatusChanged: if (status === Loader.Error) console.warn("widget failed:", modelData)
            }
        }

        MemoryWidget { Layout.leftMargin: 12 }
    }
}
