import QtQml
import waku.services as S

// Quickshell.Wayland's Toplevel: one window of the desktop, from ToplevelManager.toplevels.
//
//   appId, title, activated, minimized   what the taskbar reports; `appId` is the executable name
//                                        without ".exe", lower case (Windows has no Wayland app id)
//   className, hwnd, monitorHandle       Windows extras: window class, handle, HMONITOR (0 unknown)
//   maximized, fullscreen                always false: the taskbar does not report them
//   screens                              always empty: no monitor is mapped to a screen yet
//
// activate() raises and focuses the window, close() asks it to close.
QtObject {
    id: root

    property string appId: ""
    property string title: ""
    property string className: ""
    property real hwnd: 0
    property real monitorHandle: 0
    property bool activated: false
    property bool minimized: false
    property bool maximized: false
    property bool fullscreen: false
    property string iconUrl: ""
    property string progressState: "none"
    property real progress: 0
    property var screens: []

    // The taskbar model and the row of this window in it, set by ToplevelManager.
    property var taskbar: null
    property int row: -1

    function activate() {
        S.WindowControl.activate(Number(hwnd));
    }

    function close() {
        if (taskbar && row >= 0)
            taskbar.closeWindow(row);
    }
}
