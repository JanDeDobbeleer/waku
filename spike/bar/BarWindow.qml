import QtQuick
import QtQuick.Window

// One bar on one screen. Created by BarHost, never written by users.
Window {
    id: win

    required property Bar spec
    required property QtObject host

    // HMONITOR of the screen this bar is on, set by the host. Widgets match their window
    // manager's monitor with it: Window.window.monitorHandle.
    property var monitorHandle: 0

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    color: "transparent"
    x: screen.virtualX
    y: spec.position === Bar.Bottom ? screen.virtualY + screen.height - height : screen.virtualY
    width: screen.width
    height: spec.height
    visible: false

    Rectangle {
        objectName: "background"
        anchors.fill: parent
        color: win.spec.hasBackground ? win.spec.background : Theme.surface
    }

    Loader {
        objectName: "layout"
        anchors.fill: parent
        sourceComponent: win.spec.layout
    }

    // A failed reload keeps the previous bars; this says why.
    Rectangle {
        objectName: "errorPill"
        visible: win.host.error !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(errorText.implicitWidth + 20, parent.width - 24)
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
            text: "config error: " + win.host.error
        }
    }
}
