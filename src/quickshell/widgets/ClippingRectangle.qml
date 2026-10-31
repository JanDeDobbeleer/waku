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

    property color color: "transparent"
    property real radius: 0
    property real topLeftRadius: radius
    property real topRightRadius: radius
    property real bottomLeftRadius: radius
    property real bottomRightRadius: radius
    property alias border: frame.border
    property bool contentUnderBorder: false
    property bool contentInsideBorder: !contentUnderBorder

    default property alias data: content.data
    readonly property Item contentItem: content

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
