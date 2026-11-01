pragma ComponentBehavior: Bound
import QtQuick
import wabar
import wabar.services as S

// Battery charge. Hidden on a machine without a battery.
//
//     Battery { format: "%1%"; chargingFormat: "%1%"; lowBelow: 20 }
//
// `format` and `chargingFormat`: %1 is the charge in percent. The pill turns `lowFill` below
// `lowBelow` percent while it is not charging. Hovering shows the state and the time left.
Pill {
    id: root

    property int interval: 10000
    property string format: "%1%"
    property string chargingFormat: "%1%"
    // The status glyph by charge level; `glyphCharging` while charging (drawn in Theme.good).
    property string glyphCharging: "\uea93"
    property string glyphCritical: "\ue850"
    property string glyphLow: "\ue852"
    property string glyphMedium: "\ue855"
    property string glyphHigh: "\ue858"
    property string glyphFull: "\ue83f"
    property real lowBelow: 20
    property color lowFill: Theme.warnFill
    property color chargingFill: restFill

    property S.Battery source: S.Battery {
        interval: root.interval
    }

    function remaining(seconds) {
        if (seconds < 0)
            return "";
        const minutes = Math.round(seconds / 60);
        return minutes >= 60 ? Math.floor(minutes / 60) + " h " + (minutes % 60) + " min" : minutes + " min";
    }

    function statusGlyph() {
        if (source.charging)
            return glyphCharging;
        const p = source.percent;
        return p >= 90 ? glyphFull : p >= 60 ? glyphHigh : p >= 35 ? glyphMedium : p >= 15 ? glyphLow : glyphCritical;
    }

    visible: source.available
    icon: statusGlyph()
    iconColor: source.charging ? Theme.good : textColor
    interactive: false
    tooltip: !source.available ? "" : source.percent + "%"
             + (source.charging ? ", charging" : source.plugged ? ", plugged in" : "")
             + (!source.plugged && source.timeRemaining >= 0 ? "\n" + remaining(source.timeRemaining) + " left" : "")
    fill: source.charging ? chargingFill : (source.percent < lowBelow ? lowFill : restFill)
    text: (source.charging ? chargingFormat : format).replace("%1", source.percent)
}
