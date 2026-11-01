import QtQml
import wabar as W

// Quickshell's QsMenuOpener, so a tray menu written for Quickshell loads. A tray item's `menu` is
// null on Windows, so `children` stays empty and no entries are shown.
QtObject {
    property QtObject menu: null
    readonly property W.ScriptModel children: W.ScriptModel {}
}
