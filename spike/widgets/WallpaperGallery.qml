import QtQuick
import QtQuick.Effects
import QtQuick.Window
import wabar
import wabar.services as S

// The wallpaper gallery: a strip of thumbnails of the files in the wallpaper folder, centred on a
// dimmed full-screen window on the screen the service picked (S.Wallpapers.galleryScreen). A click
// on a thumbnail or Enter sets that wallpaper and closes the gallery; Escape, a click outside the
// strip or switching to another window closes it. Left, Right, Home, End and Page Up/Down move.
// The thumbnail of the wallpaper that is set has an accent border.
//
// Shown while S.Wallpapers.galleryOpen is true; Wallpapers.qml owns it.
Window {
    id: root

    property int imageWidth: 240
    // "portrait" cards are 16:9 tall, "landscape" cards 9:16 flat, as in the Widgets build.
    property string orientation: "portrait"
    property int cornerRadius: 0
    property color accentColor: "#ffffff"

    readonly property int cardHeight: orientation === "landscape" ? Math.floor(imageWidth * 9 / 16) : Math.floor(imageWidth * 16 / 9)
    readonly property int frame: 3
    readonly property int gap: 14
    property bool wasActive: false

    function screenNamed(name) {
        const screens = Qt.application.screens;
        for (let i = 0; i < screens.length; ++i)
            if (screens[i].name === name)
                return screens[i];
        return screens.length > 0 ? screens[0] : null;
    }

    function currentIndex() {
        const files = S.Wallpapers.files;
        for (let i = 0; i < files.length; ++i)
            if (files[i].current)
                return i;
        return 0;
    }

    function apply(path) {
        S.Wallpapers.setWallpaper(path);
        S.Wallpapers.closeGallery();
    }

    function applyCurrent() {
        const files = S.Wallpapers.files;
        if (strip.currentIndex >= 0 && strip.currentIndex < files.length)
            apply(files[strip.currentIndex].path);
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visible: S.Wallpapers.galleryOpen
    screen: screenNamed(S.Wallpapers.galleryScreen)

    onVisibleChanged: {
        if (!visible)
            return;
        const info = screen;
        if (info) {
            x = info.virtualX;
            y = info.virtualY;
            width = info.width;
            height = info.height;
        }
        wasActive = false;
        strip.currentIndex = currentIndex();
        strip.positionViewAtIndex(strip.currentIndex, ListView.Center);
        requestActivate();
        keys.forceActiveFocus();
    }

    // A window that was active and no longer is: the user went elsewhere.
    onActiveChanged: {
        if (active)
            wasActive = true;
        else if (visible && wasActive)
            S.Wallpapers.closeGallery();
    }

    Rectangle {
        objectName: "galleryBackdrop"
        anchors.fill: parent
        color: "#000000"
        opacity: root.visible ? 0.55 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: 120
            }
        }

        TapHandler {
            onTapped: S.Wallpapers.closeGallery()
        }
    }

    Item {
        id: keys

        focus: true
        Keys.onEscapePressed: S.Wallpapers.closeGallery()
        Keys.onReturnPressed: root.applyCurrent()
        Keys.onEnterPressed: root.applyCurrent()
        Keys.onLeftPressed: strip.currentIndex = Math.max(0, strip.currentIndex - 1)
        Keys.onRightPressed: strip.currentIndex = Math.min(strip.count - 1, strip.currentIndex + 1)
        Keys.onPressed: event => {
            const page = Math.max(1, Math.floor(strip.width / (root.imageWidth + root.gap)) - 1);
            if (event.key === Qt.Key_Home)
                strip.currentIndex = 0;
            else if (event.key === Qt.Key_End)
                strip.currentIndex = Math.max(0, strip.count - 1);
            else if (event.key === Qt.Key_PageUp)
                strip.currentIndex = Math.max(0, strip.currentIndex - page);
            else if (event.key === Qt.Key_PageDown)
                strip.currentIndex = Math.min(strip.count - 1, strip.currentIndex + page);
            else
                return;
            event.accepted = true;
        }
    }

    Text {
        anchors.centerIn: parent
        visible: strip.count === 0
        color: "#ffffff"
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSize
        text: "No wallpapers found in " + S.Wallpapers.imagePath
    }

    ListView {
        id: strip

        objectName: "gallery"
        anchors.centerIn: parent
        width: Math.min(contentWidth, root.width - 2 * root.gap)
        height: root.cardHeight + 2 * root.frame
        orientation: ListView.Horizontal
        spacing: root.gap
        model: S.Wallpapers.files
        clip: false
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: 150
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: width / 2 - root.imageWidth / 2
        preferredHighlightEnd: width / 2 + root.imageWidth / 2

        delegate: Item {
            id: card

            required property var modelData
            required property int index
            readonly property bool selected: ListView.isCurrentItem

            width: root.imageWidth + 2 * root.frame
            height: root.cardHeight + 2 * root.frame

            Rectangle {
                anchors.fill: parent
                radius: root.cornerRadius + root.frame
                color: "transparent"
                border.width: card.modelData.current ? root.frame : (card.selected ? 1 : 0)
                border.color: card.modelData.current ? root.accentColor : Qt.rgba(1, 1, 1, 0.6)
            }

            Item {
                id: picture

                x: root.frame
                y: root.frame
                width: root.imageWidth
                height: root.cardHeight
                opacity: card.selected ? 1 : 0.7
                layer.enabled: root.cornerRadius > 0
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: mask
                }

                Image {
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(root.imageWidth * 2, root.cardHeight * 2)
                    source: card.modelData.thumbnail
                }
            }

            Rectangle {
                id: mask

                visible: false
                width: root.imageWidth
                height: root.cardHeight
                radius: root.cornerRadius
                layer.enabled: root.cornerRadius > 0
            }

            TapHandler {
                onTapped: root.apply(card.modelData.path)
            }
            HoverHandler {
                onHoveredChanged: if (hovered)
                    strip.currentIndex = card.index
            }
        }
    }
}
