pragma ComponentBehavior: Bound
import QtQuick
import wabar
import wabar.services as S

// Memory use.
//
//     Memory { format: "RAM %2 / %3 GiB" }
//
// `glyph` is the RAM icon before the text (empty for none).
// `format`: %1 is the use in percent, %2 the used and %3 the total memory in GiB. The pill turns
// `warnFill` at `warnAbove` percent and above. Hovering shows used and total memory and the swap.
// A click opens a popup with a graph of the last `historySize` loads and stat cards (`popup: false`
// turns it off; `graph` and `graphGrid` switch the chart). A right click toggles `altFormat`,
// which takes the same placeholders as `format`.
Pill {
    id: root

    property int interval: 2000
    property string format: "RAM %1%"
    property string altFormat: "%2 / %3 GiB"
    property string glyph: ""
    property real warnAbove: 90
    property color warnFill: Theme.warnFill
    property bool popup: true
    property bool graph: true
    property bool graphGrid: true
    property int historySize: 60
    property bool showAlt: false

    property S.Memory source: S.Memory {
        interval: root.interval
        historySize: root.historySize
    }

    function gib(bytes) {
        return (bytes / 1073741824).toFixed(1);
    }

    // 17.4 GB, 160.1 MB: binary units under Python YASB's short names.
    function size(bytes) {
        if (!source.ready)
            return "—";
        const units = ["B", "KB", "MB", "GB", "TB"];
        let value = bytes;
        let unit = 0;
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024;
            ++unit;
        }
        return value.toFixed(unit === 0 ? 0 : 1) + " " + units[unit];
    }

    function fill3(template) {
        return source.ready
               ? template.replace("%1", Math.round(source.percent)).replace("%2", gib(source.used)).replace("%3", gib(source.total))
               : template.replace("%1", "…").replace("%2", "…").replace("%3", "…");
    }

    interactive: popup
    icon: glyph
    tooltip: !source.ready ? "" : "Used " + gib(source.used) + " of " + gib(source.total) + " GiB
Swap " + Math.round(source.swapPercent) + "%"
    fill: source.percent >= warnAbove ? warnFill : restFill
    text: fill3(showAlt ? altFormat : format)

    onClicked: if (popup) panel.toggle()
    onRightClicked: showAlt = !showAlt

    StatPopup {
        id: panel
        objectName: "memoryPopup"
        anchor.item: root
        boldTitle: "Memory"
        title: "Usage"
        history: root.source.history
        graph: root.graph
        graphGrid: root.graphGrid
        graphColor: "#ca9ee6"
        stats: [
            { label: "In use", value: root.size(root.source.used) },
            { label: "All memory", value: root.size(root.source.total) },
            { label: "Cached", value: root.size(root.source.cached) },
            { label: "Available", value: root.size(root.source.available) },
            { label: "Swap used", value: root.size(root.source.swapUsed) },
            { label: "Utilization", value: root.source.ready ? Math.round(root.source.percent) + "%" : "—" }
        ]
    }
}
