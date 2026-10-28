// Output volume: wheel changes it, click toggles mute.
import QtQuick
import Yasb.Services

Rectangle {
    visible: AudioOutput.available
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
        text: AudioOutput.muted ? "🔇 muted" : "🔊 " + AudioOutput.volume + "%"
    }

    HoverHandler { id: hover }
    TapHandler { onTapped: AudioOutput.muted = !AudioOutput.muted }
    WheelHandler {
        onWheel: event => AudioOutput.volume += event.angleDelta.y > 0 ? 5 : -5
    }
}
