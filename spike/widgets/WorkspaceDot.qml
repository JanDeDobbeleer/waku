import QtQuick
import tidy

// One workspace of the "dots" style of Workspaces: a small circle, or a wide accent pill while it
// is the focused workspace. A workspace that is shown on another monitor is a brighter circle, one
// with windows a dimmer one, an empty one the dimmest. Hovering brightens it.
//
//     WorkspaceDot { focused: true }
Item {
    id: root

    property bool focused: false
    property bool active: false
    property bool hasWindows: false
    property int dotSize: 12
    property int focusedWidth: 32
    // Room around the dot that still counts as the dot for hover and clicks.
    property int margin: 3
    property color focusedColor: Theme.accent
    property color activeColor: Qt.alpha(Theme.accent, 0.6)
    property color usedColor: Qt.alpha(Theme.text, 0.55)
    property color emptyColor: Qt.alpha(Theme.text, 0.25)

    signal clicked

    readonly property alias dot: dot
    readonly property color restColor: focused ? focusedColor
                                       : active ? activeColor
                                       : hasWindows ? usedColor : emptyColor

    implicitWidth: (focused ? focusedWidth : dotSize) + 2 * margin
    width: implicitWidth
    height: implicitHeight
    implicitHeight: Theme.pillHeight

    Behavior on implicitWidth {
        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }
    }

    Rectangle {
        id: dot

        anchors.centerIn: parent
        width: root.implicitWidth - 2 * root.margin
        height: root.dotSize
        radius: height / 2
        color: hover.hovered && !root.focused ? Qt.lighter(root.restColor, 1.4) : root.restColor

        Behavior on color {
            ColorAnimation {
                duration: 150
            }
        }
    }

    HoverHandler {
        id: hover
    }

    TapHandler {
        onTapped: root.clicked()
    }
}
