import QtQuick

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
// drop the hover highlight and the click handlers.
Rectangle {
    id: root

    property string text: ""
    property color textColor: "white"
    property int fontSize: 13
    property int padding: 10
    property color fill: "#22ffffff"
    property color hoverFill: "#44ffffff"
    property bool interactive: true

    default property alias content: row.data

    signal clicked
    signal rightClicked
    // `delta` is the wheel's angleDelta.y: positive away from the user.
    signal scrolled(int delta)

    implicitWidth: row.implicitWidth + 2 * padding
    implicitHeight: 24
    radius: height / 2
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
