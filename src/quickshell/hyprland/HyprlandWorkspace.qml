import QtQuick
import wabar as W

// Quickshell.Hyprland's HyprlandWorkspace, so type annotations in a shell resolve. Nothing creates
// one on Windows: the Hyprland singleton's lists are empty.
QtObject {
    property int id: 0
    property string name: ""
    property QtObject monitor: null
    property bool focused: false
    property bool active: false
    property bool urgent: false
    property W.ScriptModel toplevels: W.ScriptModel {}
    property var lastIpcObject: ({})
}
