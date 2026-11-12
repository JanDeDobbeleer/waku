pragma ComponentBehavior: Bound
import QtQuick

// Quickshell.Hyprland's HyprlandFocusGrab without the compositor protocol: while `active`, the
// listed windows stay the interactive ones, and when none of them is the active window any more
// (the user clicked or switched elsewhere) or all are hidden, the grab ends: `active` becomes
// false and cleared() is emitted. Windows are the shell's PanelWindow, FloatingWindow or
// PopupWindow objects.
//
// The grab arms when a listed window has been active, so it does not end before the window took
// the focus. A window that never becomes active (keyboardFocus None) never ends the grab.
QtObject {
    id: root

    final property bool active: false
    final property list<QtObject> windows

    signal cleared

    // Wait this long (ms) after the last window lost the focus: focus moves between two listed
    // windows pass through "none active".
    final property int graceInterval: 80

    final property bool armed: false

    function nativeOf(w) {
        return w ? (w.backingWindow ? w.backingWindow : w) : null;
    }

    function anyActive() {
        for (let i = 0; i < windows.length; ++i) {
            const n = nativeOf(windows[i]);
            if (n && n.active === true)
                return true;
        }
        return false;
    }

    function anyVisible() {
        for (let i = 0; i < windows.length; ++i) {
            const n = nativeOf(windows[i]);
            if (n && n.visible === true)
                return true;
        }
        return false;
    }

    // Re-reads the windows. The Connections below call this on every change.
    function check() {
        if (!active)
            return;
        if (anyActive()) {
            armed = true;
            grace.stop();
        } else if (armed || !anyVisible()) {
            grace.restart();
        }
    }

    function end() {
        if (!active)
            return;
        armed = false;
        active = false;
        cleared();
    }

    onActiveChanged: {
        armed = false;
        grace.stop();
        if (active)
            check();
    }
    onWindowsChanged: check()

    final readonly property Timer graceTimer: Timer {
        id: grace
        interval: root.graceInterval
        onTriggered: {
            if (!root.anyActive() && (root.armed || !root.anyVisible()))
                root.end();
        }
    }

    final readonly property Instantiator watchers: Instantiator {
        model: root.windows.length
        delegate: Item {
            final required property int index
            final readonly property QtObject nativeWindow: root.nativeOf(root.windows[index])
            Connections {
                target: nativeWindow
                ignoreUnknownSignals: true
                function onActiveChanged() {
                    root.check();
                }
                function onVisibleChanged() {
                    root.check();
                }
            }
        }
    }
}
