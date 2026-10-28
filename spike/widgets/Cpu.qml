import QtQuick
import tidy
import tidy.services as S

// Processor load.
//
//     Cpu { format: "CPU %1%"; interval: 2000; warnAbove: 90 }
//
// `format`: %1 is the load in percent. The pill turns `warnFill` at `warnAbove` percent and above.
// Hovering shows the load of every core.
Pill {
    id: root

    property int interval: 2000
    property string format: "CPU %1%"
    property real warnAbove: 90
    property color warnFill: Theme.warnFill

    property S.Cpu source: S.Cpu {
        interval: root.interval
    }

    interactive: false
    tooltip: !source.ready ? "" : source.cores + " cores"
             + (source.frequency > 0 ? " at " + (source.frequency / 1000).toFixed(1) + " GHz" : "")
             + "\n" + source.perCore.map(c => Math.round(c) + "%").join("  ")
    fill: source.percent >= warnAbove ? warnFill : Theme.fill
    text: format.replace("%1", source.ready ? Math.round(source.percent) : "…")
}
