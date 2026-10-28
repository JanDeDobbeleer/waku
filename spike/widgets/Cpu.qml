import QtQuick
import barnacle.services as S

// Processor load.
//
//     Cpu { format: "CPU %1%"; interval: 2000; warnAbove: 90 }
//
// `format`: %1 is the load in percent. The pill turns `warnFill` at `warnAbove` percent and above.
Pill {
    id: root

    property int interval: 2000
    property string format: "CPU %1%"
    property real warnAbove: 90
    property color warnFill: "#55e5484d"

    property S.Cpu source: S.Cpu {
        interval: root.interval
    }

    interactive: false
    fill: source.percent >= warnAbove ? warnFill : "#22ffffff"
    text: format.replace("%1", source.ready ? Math.round(source.percent) : "…")
}
