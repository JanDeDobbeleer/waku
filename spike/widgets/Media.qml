import QtQuick
import barnacle.services as S

// What is playing, with the album art the player delivered. A click plays or pauses, a right
// click skips to the next track, the wheel skips back and forward. Hidden when no player runs.
//
//     Media { maxWidth: 220; showArt: false }
Pill {
    id: root

    property int maxWidth: 260
    property bool showArt: true
    property string playingGlyph: "▶"
    property string pausedGlyph: "⏸"

    visible: S.Media.available

    onClicked: S.Media.playPause()
    onRightClicked: S.Media.next()
    onScrolled: delta => delta > 0 ? S.Media.previous() : S.Media.next()

    // thumbnailUrl is a data: URL of the player's own image bytes; empty when it gave none.
    Image {
        visible: root.showArt && S.Media.thumbnailUrl !== ""
        width: 18
        height: 18
        anchors.verticalCenter: parent.verticalCenter
        source: S.Media.thumbnailUrl
        sourceSize: Qt.size(36, 36)
        fillMode: Image.PreserveAspectCrop
    }

    Text {
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(implicitWidth, root.maxWidth)
        elide: Text.ElideRight
        color: root.textColor
        font.pixelSize: root.fontSize
        text: (S.Media.playing ? root.playingGlyph : root.pausedGlyph) + " " + S.Media.title
              + (S.Media.artist ? " - " + S.Media.artist : "")
    }
}
