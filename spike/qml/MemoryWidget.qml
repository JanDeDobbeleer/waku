import QtQuick
import barnacle.services

Rectangle {
    id: root

    implicitWidth: label.implicitWidth + 20
    implicitHeight: 24
    radius: 12
    color: hover.hovered ? "#44ffffff" : "#22ffffff"

    Behavior on color { ColorAnimation { duration: 150 } }

    Memory { id: memory }

    Text {
        id: label
        anchors.centerIn: parent
        color: "white"
        font.pixelSize: 13
        text: memory.ready ? "RAM " + Math.round(memory.percent) + "%" : "RAM …"
    }

    HoverHandler { id: hover }
}
