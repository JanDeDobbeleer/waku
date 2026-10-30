import QtQuick
import wabar
import wabar.services as S

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

    // The media menu of Python YASB: 400 px wide, 12 px around; the art on the left, the track,
    // the round buttons and the source tag on the right, the times and a thin seek bar below.
    PopupWindow {
        id: panel
        objectName: "mediaPopup"
        anchor.item: root
        padding: 12
        spacing: 0

        Row {
            id: top

            readonly property bool hasArt: S.Media.thumbnailUrl !== ""

            width: 376

            Image {
                visible: top.hasArt
                width: 100
                height: 100
                source: S.Media.thumbnailUrl
                sourceSize: Qt.size(200, 200)
                fillMode: Image.PreserveAspectCrop
            }

            Column {
                width: top.width - (top.hasArt ? 100 : 0)

                Text {
                    width: parent.width
                    leftPadding: 12
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    text: S.Media.title
                }

                Text {
                    width: parent.width
                    leftPadding: 12
                    elide: Text.ElideRight
                    color: Qt.alpha(Theme.text, 0.65)
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    text: S.Media.artist
                }

                Item {
                    width: parent.width
                    height: 46

                    Row {
                        x: 10
                        y: 10
                        spacing: 4

                        Repeater {
                            model: [
                                {
                                    "glyph": "",
                                    "ok": S.Media.canPrevious,
                                    "act": 0
                                },
                                {
                                    "glyph": S.Media.playing ? "" : "",
                                    "ok": S.Media.canPlayPause,
                                    "act": 1
                                },
                                {
                                    "glyph": "",
                                    "ok": S.Media.canNext,
                                    "act": 2
                                }
                            ]

                            delegate: Rectangle {
                                id: button

                                required property var modelData

                                readonly property bool isPlay: modelData.act === 1

                                width: 36
                                height: 36
                                radius: 18
                                color: isPlay ? (hover.hovered ? Qt.darker(Theme.accent, 1.1) : Theme.accent) : (hover.hovered && modelData.ok ? Qt.alpha(Theme.text, 0.1) : "transparent")
                                opacity: tap.pressed ? 0.7 : 1

                                Text {
                                    anchors.centerIn: parent
                                    color: button.isPlay ? Theme.onAccent : (button.modelData.ok ? Theme.text : "#51576d")
                                    font.family: Theme.iconFont
                                    font.pixelSize: button.isPlay ? 18 : 14
                                    text: button.modelData.glyph
                                }

                                HoverHandler {
                                    id: hover
                                }

                                TapHandler {
                                    id: tap
                                    onTapped: {
                                        if (!button.modelData.ok)
                                            return;
                                        if (button.modelData.act === 0)
                                            S.Media.previous();
                                        else if (button.modelData.act === 1)
                                            S.Media.playPause();
                                        else
                                            S.Media.next();
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible: S.Media.sourceName !== ""
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 5
                        width: sourceText.implicitWidth + 8
                        height: sourceText.implicitHeight + 4
                        radius: 3
                        color: "#babbf1"

                        Text {
                            id: sourceText
                            anchors.centerIn: parent
                            color: Theme.onAccent
                            font.family: Theme.fontFamily
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            text: S.Media.sourceName
                        }
                    }
                }
            }
        }

        Item {
            width: top.width
            height: 20 + 12

            Text {
                y: 16
                color: Qt.alpha(Theme.text, 0.55)
                font.family: Theme.fontFamily
                font.pixelSize: 12
                text: root.clock(S.Media.position)
            }

            Text {
                y: 16
                anchors.right: parent.right
                color: Qt.alpha(Theme.text, 0.55)
                font.family: Theme.fontFamily
                font.pixelSize: 12
                text: root.clock(S.Media.duration)
            }
        }

        Slider {
            objectName: "seekSlider"
            width: top.width
            implicitHeight: 14
            grooveHeight: 2
            handleSize: 10
            ringHandle: false
            handleColor: Theme.accent
            grooveColor: Qt.alpha(Theme.text, 0.15)
            from: 0
            to: Math.max(1, S.Media.duration)
            value: S.Media.position
            enabled: S.Media.duration > 0
            onMoved: v => S.Media.seek(v)
        }
    }
}
