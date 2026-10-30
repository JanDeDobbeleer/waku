import QtQuick

// Used when the very first load of shell.qml fails, so the desktop still gets a bar that shows the
// error (the error pill of every PanelWindow) instead of nothing.
ShellRoot {
    PanelWindow {
        anchors.top: true
        anchors.left: true
        anchors.right: true
        implicitHeight: 40

        Text {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: "wabar"
        }
    }
}
