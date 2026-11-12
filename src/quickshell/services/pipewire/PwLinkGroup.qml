import QtQuick

// Quickshell.Services.Pipewire's PwLinkGroup, so annotations in a shell resolve. Nothing creates
// one: Pipewire.linkGroups is always empty on Windows.
QtObject {
    final property QtObject source: null
    final property QtObject target: null
    final property int state: 0
}
