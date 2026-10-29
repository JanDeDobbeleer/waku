import QtQuick
import tidy
import tidy.services as S

// Memory use.
//
//     Memory { format: "RAM %2 / %3 GiB" }
//
// `glyph` is the RAM icon before the text (empty for none).
// `format`: %1 is the use in percent, %2 the used and %3 the total memory in GiB. The pill turns
// `warnFill` at `warnAbove` percent and above. Hovering shows used and total memory and the swap.
Pill {
    id: root

    property int interval: 2000
    property string format: "RAM %1%"
    property string glyph: "\ueea0"
    property real warnAbove: 90
    property color warnFill: Theme.warnFill

    property S.Memory source: S.Memory {
        interval: root.interval
    }

    function gib(bytes) {
        return (bytes / 1073741824).toFixed(1);
    }

    interactive: false
    icon: glyph
    tooltip: !source.ready ? "" : "Used " + gib(source.used) + " of " + gib(source.total) + " GiB\nSwap " + Math.round(source.swapPercent) + "%"
    fill: source.percent >= warnAbove ? warnFill : restFill
    text: source.ready
          ? format.replace("%1", Math.round(source.percent)).replace("%2", gib(source.used)).replace("%3", gib(source.total))
          : format.replace("%1", "…").replace("%2", "…").replace("%3", "…")
}
