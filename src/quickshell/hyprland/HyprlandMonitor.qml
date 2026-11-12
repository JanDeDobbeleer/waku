import QtQuick

// Quickshell.Hyprland's HyprlandMonitor, so type annotations in a shell resolve. Nothing creates
// one on Windows.
QtObject {
    final property int id: 0
    final property string name: ""
    final property string description: ""
    final property int x: 0
    final property int y: 0
    final property int width: 0
    final property int height: 0
    final property real scale: 1
    final property QtObject activeWorkspace: null
    final property bool focused: false
    final property var lastIpcObject: ({})
}
