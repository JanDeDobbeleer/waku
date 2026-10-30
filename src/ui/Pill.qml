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
// A Pill on a shell window is `flat` by default and takes Theme.barFill and barFillHover; a Pill in
// a popup keeps Theme.fill and fillHover, so its rows stay visible.
//
// On a panel docked to the left or right edge the Pill is `vertical`: the icon sits above the
// text, the pill is at least Theme.pillHeight wide and grows with its content up to the panel's
// thickness, and the text stays horizontal and is elided to that width. `rotateText` turns the
// text a quarter turn (top to bottom) instead, for a long label. Items declared inside a vertical
// Pill still sit in a row, below the icon and text.
Rectangle {
    id: root

    property string text: ""
    property color textColor: Theme.text
    property int fontSize: Theme.fontSize
    // Horizontal padding: Theme.barPadding on a bar (Python YASB's `.widget` padding), Theme.padding
    // in a popup.
    property int padding: flat ? (vertical ? Math.round(Theme.barPadding / 2) : Theme.barPadding)
                               : Theme.padding
    // Extra room left and right of the icon and text, in pixels (Python's `.label` margin).
    property int contentMargin: 0
    // The widest the text may be, in pixels; longer text is elided. 0 means no limit.
    property int maxTextWidth: 0
    // True on a panel docked to the left or right edge (QsWindow.window.dockVertical).
    property bool vertical: QsWindow.window !== null && QsWindow.window.dockVertical
    // A vertical Pill turns its text a quarter turn, reading top to bottom. Off by default: a
    // horizontal label is easier to read, and a 48 px panel fits "14:32".
    property bool rotateText: false
    // A glyph in Theme.iconFont before the text; empty for none.
    property string icon: ""
    property color iconColor: textColor
    // The font of `icon`; a widget whose glyph Theme.iconFont lacks sets Theme.iconFontFallback.
    property string iconFont: Theme.iconFont
    // True when the Pill sits in a shell window (QsWindow.window: a PanelWindow or a
    // FloatingWindow), false in a popup.
    property bool flat: QsWindow.window !== null
    // The resting and hover background; `restFill` is what a widget returns to when it has nothing
    // to signal.
    readonly property color restFill: flat ? Theme.barFill : Theme.fill
    property color fill: restFill
    property color hoverFill: flat ? Theme.barFillHover : Theme.fillHover
    property bool interactive: true
    // Hover text; empty for none.
    property string tooltip: ""

    default property alias content: row.data

    // ---- vertical layout ----
    // The widest a vertical Pill may be: the panel's thickness less a margin on each side. 0 when
    // the panel is not known.
    readonly property real maxVerticalWidth: vertical && QsWindow.window !== null
                                             && QsWindow.window.width > 0
                                             ? Math.max(Theme.pillHeight, QsWindow.window.width - Theme.spacing)
                                             : 0
    // The widest the icon, the text or the items may be inside a vertical Pill.
    readonly property real maxContentWidth: maxVerticalWidth > 0
                                            ? Math.max(0, maxVerticalWidth - 2 * padding - 2 * contentMargin)
                                            : 100000
    // The width of the column of icon and text.
    readonly property real columnWidth: Math.max(vIcon.implicitWidth, vLabelBox.implicitWidth)

    signal clicked
    signal rightClicked
    // `delta` is the wheel's angleDelta.y: positive away from the user.
    signal scrolled(int delta)

    implicitWidth: vertical
                   ? Math.max(Theme.pillHeight,
                              Math.min(Math.max(vcol.implicitWidth, row.implicitWidth) + 2 * padding + 2 * contentMargin,
                                       maxVerticalWidth > 0 ? maxVerticalWidth : 100000))
                   : row.implicitWidth + 2 * padding + 2 * contentMargin
    implicitHeight: vertical ? Math.max(Theme.pillHeight, body.implicitHeight + 2 * Math.round(Theme.pillHeight / 6))
                             : Theme.pillHeight
    radius: Theme.radius >= 0 ? Math.min(Theme.radius, height / 2) : height / 2
    color: interactive && hover.hovered ? hoverFill : fill

    Behavior on color {
        ColorAnimation {
            duration: 150
        }
    }

    // The icon and text of a vertical Pill, above the row of the items declared inside it. On a
    // horizontal Pill only `row` shows and `body` is the size of `row`.
    Column {
        id: body
        anchors.centerIn: parent
        spacing: 2

        Column {
            id: vcol
            visible: root.vertical
            spacing: 2

            Item {
                visible: root.icon !== ""
                width: root.columnWidth
                height: vIcon.implicitHeight

                Text {
                    id: vIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: root.iconColor
                    font.family: root.iconFont
                    font.pixelSize: Theme.iconSize
                    text: root.icon
                }
            }

            Item {
                id: vLabelBox
                visible: root.text !== ""
                implicitWidth: root.rotateText ? vLabel.implicitHeight : vLabel.width
                implicitHeight: root.rotateText ? vLabel.width : vLabel.implicitHeight
                width: Math.max(implicitWidth, root.columnWidth)
                height: implicitHeight

                Text {
                    id: vLabel
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth,
                                    root.rotateText
                                        ? (root.maxTextWidth > 0 ? root.maxTextWidth : 100000)
                                        : (root.maxTextWidth > 0 ? Math.min(root.maxTextWidth, root.maxContentWidth)
                                                                 : root.maxContentWidth))
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    rotation: root.rotateText ? 90 : 0
                    color: root.textColor
                    font.family: Theme.fontFamily
                    font.pixelSize: root.fontSize
                    font.weight: Theme.fontWeight
                    text: root.text
                }
            }
        }

        Row {
            id: row
            // An empty row takes no space in a vertical Pill.
            visible: !root.vertical || row.implicitWidth > 0
            spacing: root.flat ? Theme.iconSpacing : 6

            Text {
                visible: root.icon !== "" && !root.vertical
                anchors.verticalCenter: parent.verticalCenter
                color: root.iconColor
                font.family: root.iconFont
                font.pixelSize: Theme.iconSize
                text: root.icon
            }

            Text {
                visible: root.text !== "" && !root.vertical
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
