import QtQml
import waku as W

// Quickshell's QsMenuOpener, so a tray menu written for Quickshell loads. A tray item's `menu` is
// null on Windows, so `children` stays empty and no entries are shown.
QtObject {
    final property QtObject menu: null
    final readonly property W.ScriptModel children: W.ScriptModel {}
}
