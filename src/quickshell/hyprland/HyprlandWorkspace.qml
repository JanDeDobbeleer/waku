import QtQuick
import waku as W

// Quickshell.Hyprland's HyprlandWorkspace, so type annotations in a shell resolve. Nothing creates
// one on Windows: the Hyprland singleton's lists are empty.
QtObject {
    final property int id: 0
    final property string name: ""
    final property QtObject monitor: null
    final property bool focused: false
    final property bool active: false
    final property bool urgent: false
    final property W.ScriptModel toplevels: W.ScriptModel {}
    final property var lastIpcObject: ({})
}
