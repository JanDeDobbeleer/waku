import QtQuick

// Quickshell.Hyprland's HyprlandMonitor, so type annotations in a shell resolve. Nothing creates
// one on Windows.
QtObject {
    property int id: 0
    property string name: ""
    property string description: ""
    property int x: 0
    property int y: 0
    property int width: 0
    property int height: 0
    property real scale: 1
    property QtObject activeWorkspace: null
    property bool focused: false
    property var lastIpcObject: ({})
}
