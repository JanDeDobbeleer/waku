import QtQuick
import QtQuick.Effects

// Quickshell.Widgets ClippingRectangle: a rectangle that clips its children to its rounded shape.
//
//     ClippingRectangle { radius: 12; color: "#222"; Image { anchors.fill: parent; source: "a.png" } }
//
// The fill is drawn under the children, the border above them. The clip is a MultiEffect mask, so
// it costs a layer: use it only where children must follow the corners. Children go into
// `contentItem`, inset by the border width unless contentUnderBorder is set.
Item {
    id: root

    final property color color: "transparent"
    final property real radius: 0
    final property real topLeftRadius: radius
    final property real topRightRadius: radius
    final property real bottomLeftRadius: radius
    final property real bottomRightRadius: radius
    final property alias border: frame.border
    final property bool contentUnderBorder: false
    final property bool contentInsideBorder: !contentUnderBorder

    default final property alias data: content.data
    final readonly property Item contentItem: content

    antialiasing: topLeftRadius > 0 || topRightRadius > 0 || bottomLeftRadius > 0 || bottomRightRadius > 0

    Rectangle {
        anchors.fill: parent
        color: root.color
        topLeftRadius: root.topLeftRadius
        topRightRadius: root.topRightRadius
        bottomLeftRadius: root.bottomLeftRadius
        bottomRightRadius: root.bottomRightRadius
        antialiasing: root.antialiasing
    }

    Item {
        id: clipped
        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        Item {
            id: content
            anchors.fill: parent
            anchors.margins: root.contentInsideBorder ? frame.border.width : 0
        }
    }

    Item {
        id: mask
        anchors.fill: parent
        visible: false
        layer.enabled: true
        layer.smooth: true
        Rectangle {
            anchors.fill: parent
            color: "black"
            topLeftRadius: root.topLeftRadius
            topRightRadius: root.topRightRadius
            bottomLeftRadius: root.bottomLeftRadius
            bottomRightRadius: root.bottomRightRadius
            antialiasing: true
        }
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        color: "transparent"
        topLeftRadius: root.topLeftRadius
        topRightRadius: root.topRightRadius
        bottomLeftRadius: root.bottomLeftRadius
        bottomRightRadius: root.bottomRightRadius
        antialiasing: root.antialiasing
        border.width: 0
    }
}
