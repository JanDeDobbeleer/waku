import QtQuick
import barnacle
import barnacle.services as S

// What is playing, with the album art the player delivered. A click opens a panel (art, track,
// seek bar, previous, play and pause, next), a right click plays or pauses, the wheel skips back
// and forward. Hovering shows the track. Hidden when no player runs.
//
//     Media { maxWidth: 220; showArt: false; popup: false }
//
// With `popup: false` a click plays or pauses instead of opening the panel.
Pill {
    id: root

    property int maxWidth: 260
    property bool showArt: true
    property bool popup: true
    property string playingGlyph: "▶"
    property string pausedGlyph: "⏸"

    function clock(seconds) {
        const s = Math.max(0, Math.floor(seconds));
        return Math.floor(s / 60) + ":" + ("0" + (s % 60)).slice(-2);
    }

    visible: S.Media.available
    tooltip: S.Media.title + (S.Media.artist ? "\n" + S.Media.artist : "") + (S.Media.album ? "\n" + S.Media.album : "")
             + (S.Media.sourceName ? "\n" + S.Media.sourceName : "")

    onClicked: popup ? panel.toggle() : S.Media.playPause()
    onRightClicked: S.Media.playPause()
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
        font.family: Theme.fontFamily
        font.pixelSize: root.fontSize
        text: (S.Media.playing ? root.playingGlyph : root.pausedGlyph) + " " + S.Media.title
              + (S.Media.artist ? " - " + S.Media.artist : "")
    }

    PopupWindow {
        id: panel
        objectName: "mediaPopup"
        anchorItem: root
        spacing: 10

        Row {
            spacing: 12

            Image {
                visible: S.Media.thumbnailUrl !== ""
                width: 72
                height: 72
                source: S.Media.thumbnailUrl
                sourceSize: Qt.size(144, 144)
                fillMode: Image.PreserveAspectCrop
            }

            Column {
                width: 210
                spacing: 2

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: true
                    text: S.Media.title
                }

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: Theme.text
                    opacity: 0.8
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    text: S.Media.artist
                }

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: Theme.text
                    opacity: 0.6
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFontSize
                    text: S.Media.album
                }

                Text {
                    width: parent.width
                    elide: Text.ElideRight
                    color: Theme.text
                    opacity: 0.45
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFontSize
                    text: S.Media.sourceName
                }
            }
        }

        Row {
            spacing: 8

            Text {
                width: 40
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                text: root.clock(S.Media.position)
            }

            Slider {
                objectName: "seekSlider"
                width: 206
                from: 0
                to: Math.max(1, S.Media.duration)
                value: S.Media.position
                enabled: S.Media.duration > 0
                onMoved: v => S.Media.seek(v)
            }

            Text {
                width: 40
                horizontalAlignment: Text.AlignRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                text: root.clock(S.Media.duration)
            }
        }

        Row {
            spacing: 8

            Pill {
                text: "⏮"
                opacity: S.Media.canPrevious ? 1 : 0.4
                onClicked: S.Media.previous()
            }

            Pill {
                text: S.Media.playing ? "⏸" : "▶"
                opacity: S.Media.canPlayPause ? 1 : 0.4
                onClicked: S.Media.playPause()
            }

            Pill {
                text: "⏭"
                opacity: S.Media.canNext ? 1 : 0.4
                onClicked: S.Media.next()
            }
        }
    }
}
