import QtQuick

// Quickshell.Hyprland's HyprlandEvent, so type annotations in a shell resolve. The Hyprland
// singleton never emits one on Windows.
QtObject {
    final property string name: ""
    final property string data: ""

    // The data split on commas into at most `count` parts, like Hyprland event payloads.
    function parse(count) {
        const parts = data.split(",");
        if (count <= 0 || parts.length <= count)
            return parts;
        return parts.slice(0, count - 1).concat([parts.slice(count - 1).join(",")]);
    }
}
