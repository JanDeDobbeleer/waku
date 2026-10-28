import QtQuick
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: bar

    property int barHeight: 40

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
        spacing: 12

        Clock {}

        Item { Layout.fillWidth: true }

        // User widgets: every .qml file in the widgets folder, reloaded when it changes.
        Repeater {
            model: Host.widgetFiles
            delegate: Loader {
                source: modelData
                onStatusChanged: if (status === Loader.Error) console.warn("widget failed:", modelData)
            }
        }

        MemoryWidget {}
    }
}
