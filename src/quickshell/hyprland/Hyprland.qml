pragma Singleton
import QtQuick
import waku as W

// Quickshell.Hyprland's `Hyprland` singleton, for QML written for Quickshell. Windows has no
// Hyprland: this is a compile-compatibility shim, not an implementation. The lists are empty
// models, the focused objects are null, dispatch and the refresh functions do nothing and rawEvent
// never fires, so a shell that reads them loads and shows its "no compositor" state.
QtObject {
    final readonly property string requestSocketPath: ""
    final readonly property string eventSocketPath: ""
    final readonly property bool usingLua: false

    final readonly property W.ScriptModel workspaces: W.ScriptModel {}
    final readonly property W.ScriptModel monitors: W.ScriptModel {}
    final readonly property W.ScriptModel toplevels: W.ScriptModel {}

    final readonly property QtObject activeToplevel: null
    final readonly property QtObject focusedMonitor: null
    final readonly property QtObject focusedWorkspace: null

    // Never emitted: there is no event socket.
    signal rawEvent(var event)

    function dispatch(request) {
    }

    function monitorFor(screen) {
        return null;
    }

    function refreshMonitors() {
    }

    function refreshToplevels() {
    }

    function refreshWorkspaces() {
    }
}
