import QtQuick

Rectangle {
    id: root

    implicitWidth: label.implicitWidth + 20
    implicitHeight: 24
    radius: 12
    color: hover.hovered ? "#44ffffff" : "#22ffffff"

    Behavior on color { ColorAnimation { duration: 150 } }

    Text {
        id: label
        anchors.centerIn: parent
        color: "white"
        font.pixelSize: 13
        text: "RAM " + Math.round(Host.memoryPercent) + "%"
    }

    HoverHandler { id: hover }
}
