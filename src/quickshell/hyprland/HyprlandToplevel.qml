import QtQuick

// Quickshell.Hyprland's HyprlandToplevel, so type annotations in a shell resolve. Nothing creates
// one on Windows.
QtObject {
    final property string address: ""
    final property string title: ""
    final property QtObject workspace: null
    final property QtObject monitor: null
    final property bool activated: false
    final property QtObject wayland: null
    final property var lastIpcObject: ({})
}
