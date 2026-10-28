// A user widget: no C++, no registration. Edit and save; the bar reloads it.
import QtQuick

Rectangle {
    implicitWidth: row.implicitWidth + 20
    implicitHeight: 24
    radius: 12
    color: "#335b8def"

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Text { text: "hello from a user widget"; color: "white"; font.pixelSize: 13 }
        Text { text: Host.memoryPercent.toFixed(0) + "% mem"; color: "#cde"; font.pixelSize: 13 }
    }
}
