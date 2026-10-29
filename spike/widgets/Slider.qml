import QtQuick
import tidy

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

    // Look. The default is the control center's: a 4 px muted groove, an accent filled part and a
    // round handle with a ring. The media panel sets a thin groove and a small accent handle.
    property real grooveHeight: 4
    property real handleSize: 14
    property color grooveColor: Qt.alpha(Theme.text, 0.7)
    property color fillColor: Theme.accent
    // The handle: a ring around a dot (the default), or a plain dot in `handleColor`.
    property bool ringHandle: true
    property color handleColor: Theme.text

    implicitWidth: 180
    implicitHeight: Math.max(20, handleSize + 4)

    readonly property real ratio: to > from ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0

    function valueAt(x) {
        const r = Math.max(0, Math.min(1, x / width));
        return from + r * (to - from);
    }

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: root.grooveHeight
        radius: height / 2
        color: root.grooveColor

        Rectangle {
            width: root.ratio * (parent.width - root.handleSize) + root.handleSize / 2
            height: parent.height
            radius: parent.radius
            color: root.fillColor
        }
    }

    Rectangle {
        id: handle
        x: root.ratio * (root.width - width)
        anchors.verticalCenter: parent.verticalCenter
        width: root.handleSize
        height: root.handleSize
        radius: width / 2
        color: root.ringHandle ? Qt.alpha(Theme.text, 0.55) : root.handleColor
        opacity: root.enabled ? 1 : 0.4

        Rectangle {
            visible: root.ringHandle
            anchors.centerIn: parent
            width: parent.width * 0.55
            height: width
            radius: width / 2
            color: Theme.text
        }
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
