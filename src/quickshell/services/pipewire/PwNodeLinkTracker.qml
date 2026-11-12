import QtQuick

// Quickshell.Services.Pipewire's PwNodeLinkTracker. Windows has no routing graph, so `linkGroups`
// is always empty.
QtObject {
    final property QtObject node: null
    final readonly property var linkGroups: []
}
