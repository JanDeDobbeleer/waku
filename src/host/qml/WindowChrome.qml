import QtQuick

// What waku adds to every shell window (internal, created by QsWindow): the background, and on a
// PanelWindow the error pill of a failed reload. Never written by users.
Item {
    id: chrome

    required property QtObject shellWindow

    readonly property QtObject host: shellWindow.host

    anchors.fill: parent
    z: -1000000

    Rectangle {
        objectName: "background"
        anchors.fill: parent
        // `color` when set; otherwise the waku surface. With islands the layout draws its own
        // islands (PanelLayout) and the wallpaper shows between them.
        color: chrome.shellWindow.hasColor ? chrome.shellWindow.color
             : Theme.islands ? "transparent"
             : chrome.shellWindow.hasBackground ? chrome.shellWindow.background : Theme.surface
    }

    // A failed reload keeps the previous shell; this says why.
    Rectangle {
        objectName: "errorPill"
        parent: chrome.parent
        z: 1000000
        visible: chrome.shellWindow.showsErrors && chrome.host !== null && chrome.host.error !== ""
        anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        width: Math.min(errorText.implicitWidth + 20, (parent ? parent.width : 0) - 24)
        height: 24
        radius: 12
        color: Theme.warn

        Text {
            id: errorText
            anchors.centerIn: parent
            width: parent.width - 20
            elide: Text.ElideRight
            color: "white"
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            text: "config error: " + (chrome.host ? chrome.host.error : "")
        }
    }
}
