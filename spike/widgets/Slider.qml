import QtQuick
import barnacle

// A horizontal slider for popups: a track, a filled part and a handle.
//
//     Slider { from: 0; to: 100; value: 40; onMoved: v => console.log(v) }
//
// `moved` is emitted while the user drags or clicks, with the new value; `value` itself is only
// ever changed by the code that owns it, so it can follow a service.
Item {
    id: root

    property real from: 0
    property real to: 100
    property real value: 0
    property bool enabled: true

    signal moved(real value)

    implicitWidth: 180
    implicitHeight: 20

    readonly property real ratio: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0

    function valueAt(x) {
        const r = Math.max(0, Math.min(1, x / width));
        return from + r * (to - from);
    }

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 6
        radius: 3
        color: Theme.fill

        Rectangle {
            width: root.ratio * parent.width
            height: parent.height
            radius: parent.radius
            color: Theme.accent
        }
    }

    Rectangle {
        x: root.ratio * (root.width - width)
        anchors.verticalCenter: parent.verticalCenter
        width: 14
        height: 14
        radius: 7
        color: Theme.text
        opacity: root.enabled ? 1 : 0.4
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled
        onPressed: mouse => root.moved(root.valueAt(mouse.x))
        onPositionChanged: mouse => {
            if (pressed)
                root.moved(root.valueAt(mouse.x));
        }
    }
}
