import QtQuick
import wabar
import wabar.services as S

// Processor load.
//
//     Cpu { format: "CPU %1%"; interval: 2000; warnAbove: 90 }
//
// `glyph` is the chip icon before the text (empty for none); with it, `format: "%1%"` reads like Python YASB.
// `format`: %1 is the load in percent. The pill turns `warnFill` at `warnAbove` percent and above.
// A click opens a popup with a graph of the last `historySize` loads and stat cards (`popup: false`
// turns it off; `graph` and `graphGrid` switch the chart). A right click toggles `altFormat`
// (%1 load in percent, %2 frequency in MHz). Hovering shows the load of every core.
Pill {
    id: root

    property int interval: 2000
    property string format: "CPU %1%"
    property string altFormat: "%2 MHz"
    property string glyph: ""
    property real warnAbove: 90
    property color warnFill: Theme.warnFill
    property bool popup: true
    property bool graph: true
    property bool graphGrid: true
    property int historySize: 60
    property bool showAlt: false

    property S.Cpu source: S.Cpu {
        interval: root.interval
        historySize: root.historySize
    }

    function fill2(template) {
        return template.replace("%1", source.ready ? Math.round(source.percent) : "…")
                       .replace("%2", source.ready ? Math.round(source.frequency) : "…");
    }

    function mhz(value) {
        return source.ready ? Math.round(value) + " MHz" : "—";
    }

    interactive: popup
    icon: glyph
    tooltip: !source.ready ? "" : source.cores + " cores"
             + (source.frequency > 0 ? " at " + (source.frequency / 1000).toFixed(1) + " GHz" : "")
             + "
" + source.perCore.map(c => Math.round(c) + "%").join("  ")
    fill: source.percent >= warnAbove ? warnFill : restFill
    text: fill2(showAlt ? altFormat : format)

    onClicked: if (popup) panel.toggle()
    onRightClicked: showAlt = !showAlt

    StatPopup {
        id: panel
        objectName: "cpuPopup"
        anchor.item: root
        boldTitle: "CPU"
        title: "Usage"
        history: root.source.history
        graph: root.graph
        graphGrid: root.graphGrid
        graphColor: Theme.accent
        stats: [
            { label: "Usage", value: root.source.ready ? Math.round(root.source.percent) + "%" : "—" },
            { label: "Frequency", value: root.mhz(root.source.frequency) },
            { label: "Cores (P / L)", value: root.source.ready ? root.source.coresPhysical + " / " + root.source.cores : "—" },
            { label: "Max frequency", value: root.mhz(root.source.maxFrequency) }
        ]
    }
}
