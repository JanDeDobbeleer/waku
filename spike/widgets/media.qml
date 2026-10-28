// Now playing: click toggles play and pause. Hidden when no media session exists.
import QtQuick
import Yasb.Services

Rectangle {
    visible: Media.available
    implicitWidth: Math.min(label.implicitWidth, 280) + 20
    implicitHeight: 24
    radius: 12
    color: hover.hovered ? "#44ffffff" : "#22ffffff"
    Behavior on color { ColorAnimation { duration: 150 } }

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, 280)
        elide: Text.ElideRight
        color: "white"
        font.pixelSize: 13
        text: (Media.playing ? "▶ " : "⏸ ") + Media.title + (Media.artist ? " - " + Media.artist : "")
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: Media.playPause() }
}
