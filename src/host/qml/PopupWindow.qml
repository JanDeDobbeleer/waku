import QtQuick

// A window that opens next to a widget: a calendar, a volume slider, a track list.
//
//     Pill {
//         id: pill
//         text: "Volume"
//         onClicked: popup.toggle()
//
//         PopupWindow {
//             id: popup
//             anchor.item: pill
//             Text { text: "hello"; color: Theme.text }
//         }
//     }
//
// `anchor` follows Quickshell: `anchor.item`, `anchor.rect` (a part of the item), `anchor.edges`
// and `anchor.gravity` (Edges.Top opens above, Edges.Bottom below). By default it opens just
// outside the panel (below a top panel, above a bottom one, right of a left one, left of a right
// one; below in a window that is not a docked panel), centred on the widget and kept inside the
// screen. A click in the panel, a click in another window, Escape or `close()` closes it.
// Whatever you put inside is stacked in a column and sizes the window; colours come from Theme
// (popupSurface, popupBorder).
PopupWindowBase {
    id: popup

    // Chrome, from Theme (popupGap, popupRadius, popupBorderWidth, popupFadeMs); set one on a popup to
    // override it. A popup that lays out its own sections sets `padding: 0`.
    property int padding: Theme.padding
    property real spacing: 6

    default property alias content: column.data

    contentWidth: surface.implicitWidth
    contentHeight: surface.implicitHeight

    Rectangle {
        id: surface

        objectName: "popupSurface"
        implicitWidth: column.implicitWidth + 2 * popup.padding
        implicitHeight: column.implicitHeight + 2 * popup.padding
        width: popup.width
        height: popup.height
        color: Theme.popupSurface
        border.color: Theme.popupBorder
        border.width: Theme.popupBorderWidth
        radius: Theme.popupRadius
        opacity: popup.visible ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: Theme.popupFadeMs
            }
        }

        Column {
            id: column
            x: popup.padding
            y: popup.padding
            spacing: popup.spacing
        }

        Item {
            focus: true
            Keys.onEscapePressed: popup.close()
        }
    }
}
