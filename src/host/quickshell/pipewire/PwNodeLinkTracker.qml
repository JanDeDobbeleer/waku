import QtQuick

// Quickshell.Services.Pipewire's PwNodeLinkTracker. Windows has no routing graph, so `linkGroups`
// is always empty.
QtObject {
    property QtObject node: null
    readonly property var linkGroups: []
}
