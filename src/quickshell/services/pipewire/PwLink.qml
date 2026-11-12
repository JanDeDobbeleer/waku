import QtQuick

// Quickshell.Services.Pipewire's PwLink, so annotations in a shell resolve. Nothing creates one:
// Pipewire.links is always empty on Windows.
QtObject {
    final property int id: 0
    final property QtObject source: null
    final property QtObject target: null
    final property int state: 0
}
