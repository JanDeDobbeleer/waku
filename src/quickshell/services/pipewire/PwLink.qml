import QtQuick

// Quickshell.Services.Pipewire's PwLink, so annotations in a shell resolve. Nothing creates one:
// Pipewire.links is always empty on Windows.
QtObject {
    property int id: 0
    property QtObject source: null
    property QtObject target: null
    property int state: 0
}
