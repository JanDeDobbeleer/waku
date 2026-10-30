import QtQuick

// Quickshell.Hyprland's HyprlandToplevel, so type annotations in a shell resolve. Nothing creates
// one on Windows.
QtObject {
    property string address: ""
    property string title: ""
    property QtObject workspace: null
    property QtObject monitor: null
    property bool activated: false
    property QtObject wayland: null
    property var lastIpcObject: ({})
}
