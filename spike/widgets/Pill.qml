import QtQuick
import barnacle

// The building block of the built-in widgets, and of yours: a rounded background with optional
// text and any extra items beside it.
//
//     Pill {
//         text: "hello"
//         onClicked: console.log("clicked")
//     }
//
// Items declared inside a Pill sit in a row after the text; give them
// `anchors.verticalCenter: parent.verticalCenter`. Static widgets set `interactive: false` to
// drop the hover highlight and the click handlers. Colours, font and size come from Theme.
Rectangle {
    id: root

    property string text: ""
    property color textColor: Theme.text
    property int fontSize: Theme.fontSize
    property int padding: Theme.padding
    property color fill: Theme.fill
    property color hoverFill: Theme.fillHover
    property bool interactive: true

    default property alias content: row.data

    signal clicked
    signal rightClicked
    // `delta` is the wheel's angleDelta.y: positive away from the user.
    signal scrolled(int delta)

    implicitWidth: row.implicitWidth + 2 * padding
    implicitHeight: Theme.pillHeight
    radius: Theme.radius >= 0 ? Math.min(Theme.radius, height / 2) : height / 2
    color: interactive && hover.hovered ? hoverFill : fill

    Behavior on color {
        ColorAnimation {
            duration: 150
        }
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6

        Text {
            visible: root.text !== ""
            anchors.verticalCenter: parent.verticalCenter
            color: root.textColor
            font.family: Theme.fontFamily
            font.pixelSize: root.fontSize
            text: root.text
        }
    }

    HoverHandler {
        id: hover
        enabled: root.interactive
    }

    TapHandler {
        enabled: root.interactive
        onTapped: root.clicked()
    }

    TapHandler {
        enabled: root.interactive
        acceptedButtons: Qt.RightButton
        onTapped: root.rightClicked()
    }

    WheelHandler {
        enabled: root.interactive
        onWheel: event => root.scrolled(event.angleDelta.y)
    }
}
