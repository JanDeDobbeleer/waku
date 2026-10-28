// Now playing with the album art the player delivered. Click toggles play and pause. Hidden when
// no media session exists.
import QtQuick
import Yasb.Services

Rectangle {
    visible: Media.available
    implicitWidth: row.implicitWidth + 16
    implicitHeight: 24
    radius: 12
    color: hover.hovered ? "#44ffffff" : "#22ffffff"
    Behavior on color { ColorAnimation { duration: 150 } }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        // thumbnailUrl is a data: URL of the player's own image bytes; empty when it gave none.
        Image {
            visible: Media.thumbnailUrl !== ""
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            source: Media.thumbnailUrl
            sourceSize: Qt.size(36, 36)
            fillMode: Image.PreserveAspectCrop
        }

        Text {
            width: Math.min(implicitWidth, 260)
            elide: Text.ElideRight
            color: "white"
            font.pixelSize: 13
            text: (Media.playing ? "▶ " : "⏸ ") + Media.title + (Media.artist ? " - " + Media.artist : "")
        }
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: Media.playPause() }
}
