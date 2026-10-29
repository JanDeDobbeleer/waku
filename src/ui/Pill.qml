import QtQuick
import QtQuick.Window
import wabar

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
// drop the hover highlight and the click handlers. `tooltip` is the hover text. Colours, font and
// size come from Theme.
//
// `icon` is a glyph drawn before the text in Theme.iconFont at Theme.iconSize, in `iconColor`.
// A Pill on a bar window is `flat` by default and takes Theme.barFill and barFillHover; a Pill in
// a popup keeps Theme.fill and fillHover, so its rows stay visible.
Rectangle {
    id: root

    property string text: ""
    property color textColor: Theme.text
    property int fontSize: Theme.fontSize
    // Horizontal padding: Theme.barPadding on a bar (Python YASB's `.widget` padding), Theme.padding
    // in a popup.
    property int padding: flat ? Theme.barPadding : Theme.padding
    // Extra room left and right of the icon and text, in pixels (Python's `.label` margin).
    property int contentMargin: 0
    // The widest the text may be, in pixels; longer text is elided. 0 means no limit.
    property int maxTextWidth: 0
    // A glyph in Theme.iconFont before the text; empty for none.
    property string icon: ""
    property color iconColor: textColor
    // The font of `icon`; a widget whose glyph Theme.iconFont lacks sets Theme.iconFontFallback.
    property string iconFont: Theme.iconFont
    // True when the Pill sits on a bar window (the window has a `spec`), false in a popup.
    property bool flat: Window.window !== null && Window.window.spec !== undefined
    // The resting and hover background; `restFill` is what a widget returns to when it has nothing
    // to signal.
    readonly property color restFill: flat ? Theme.barFill : Theme.fill
    property color fill: restFill
    property color hoverFill: flat ? Theme.barFillHover : Theme.fillHover
    property bool interactive: true
    // Hover text; empty for none.
    property string tooltip: ""

    default property alias content: row.data

    signal clicked
    signal rightClicked
    // `delta` is the wheel's angleDelta.y: positive away from the user.
    signal scrolled(int delta)

    implicitWidth: row.implicitWidth + 2 * padding + 2 * contentMargin
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
        spacing: root.flat ? Theme.iconSpacing : 6

        Text {
            visible: root.icon !== ""
            anchors.verticalCenter: parent.verticalCenter
            color: root.iconColor
            font.family: root.iconFont
            font.pixelSize: Theme.iconSize
            text: root.icon
        }

        Text {
            visible: root.text !== ""
            anchors.verticalCenter: parent.verticalCenter
            // Python's label padding-bottom lifts the text by half of it
            anchors.verticalCenterOffset: root.flat ? -Theme.labelPaddingBottom / 2 : 0
            width: root.maxTextWidth > 0 ? Math.min(implicitWidth, root.maxTextWidth) : implicitWidth
            elide: Text.ElideRight
            color: root.textColor
            font.family: Theme.fontFamily
            font.pixelSize: root.fontSize
            font.weight: Theme.fontWeight
            text: root.text
        }
    }

    Tooltip {
        text: root.tooltip
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
