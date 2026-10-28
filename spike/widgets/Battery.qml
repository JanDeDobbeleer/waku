import QtQuick
import barnacle
import barnacle.services as S

// Battery charge. Hidden on a machine without a battery.
//
//     Battery { format: "%1%"; chargingFormat: "⚡ %1%"; lowBelow: 20 }
//
// `format` and `chargingFormat`: %1 is the charge in percent. The pill turns `lowFill` below
// `lowBelow` percent while it is not charging.
Pill {
    id: root

    property int interval: 10000
    property string format: "%1%"
    property string chargingFormat: "⚡ %1%"
    property real lowBelow: 20
    property color lowFill: Theme.warnFill
    property color chargingFill: Theme.goodFill

    property S.Battery source: S.Battery {
        interval: root.interval
    }

    visible: source.available
    interactive: false
    fill: source.charging ? chargingFill : (source.percent < lowBelow ? lowFill : Theme.fill)
    text: (source.charging ? chargingFormat : format).replace("%1", source.percent)
}
