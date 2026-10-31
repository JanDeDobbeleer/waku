import QtQuick

// Quickshell.Services.Pipewire's PwLinkGroup, so annotations in a shell resolve. Nothing creates
// one: Pipewire.linkGroups is always empty on Windows.
QtObject {
    property QtObject source: null
    property QtObject target: null
    property int state: 0
}
