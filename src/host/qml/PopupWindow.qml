import QtQuick
import QtQuick.Window

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
Window {
    id: popup

    // What the popup opens next to.
    readonly property PopupAnchor anchor: PopupAnchor {}
    // Chrome, from Theme (popupGap, popupRadius, popupBorderWidth, popupFadeMs); set one on a popup to
    // override it. A popup that lays out its own sections sets `padding: 0`.
    property real gap: Theme.popupGap
    property real margin: 8
    property int padding: Theme.padding
    property real spacing: 6

    default property alias content: column.data

    signal opened
    signal closed

    // The window the anchor item is in, and the shell window behind it (null in a popup).
    readonly property var bar: anchor.item ? anchor.item.Window.window : null
    readonly property var shellWindow: anchor.item ? anchor.item.QsWindow.window : null
    // When it last closed, so that the click that closes it (a press outside, on the widget) does
    // not reopen it through the widget's own click handler.
    property double closedAt: 0

    // Away from the edge the panel is docked to (QsWindow.dockEdge): below a top panel, above a
    // bottom one, right of a left one, left of a right one. Below in any other window.
    function defaultEdge() {
        return PopupPlacement.edgeForDock(shellWindow ? shellWindow.dockEdge : DockEdge.None);
    }

    // The screen the anchor item is on, as a Window takes it (Qt.application.screens).
    function screenOf(item) {
        const name = item.Screen.name;
        const list = Qt.application.screens;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].name === name)
                return list[i];
        }
        return null;
    }

    function reposition() {
        const item = anchor.item;
        if (!item || !bar)
            return;
        const screenInfo = item.Screen;
        let rect;
        if (anchor.hasRect) {
            const origin = item.mapToGlobal(anchor.rect.x, anchor.rect.y);
            rect = Qt.rect(origin.x, origin.y, anchor.rect.width, anchor.rect.height);
        } else {
            const origin = item.mapToGlobal(0, 0);
            // The item across the thickness of its panel, so the popup clears the whole panel.
            rect = PopupPlacement.anchorRect(Qt.rect(origin.x, origin.y, item.width, item.height),
                                             Qt.rect(bar.x, bar.y, bar.width, bar.height),
                                             shellWindow ? shellWindow.dockVertical : false);
        }
        const spot = PopupPlacement.position(rect, Qt.size(width, height),
                                             Qt.rect(screenInfo.virtualX, screenInfo.virtualY, screenInfo.width, screenInfo.height),
                                             anchor.placementEdge(defaultEdge()), gap, margin);
        x = spot.x;
        y = spot.y;
    }

    function open() {
        if (!anchor.item || !bar)
            return;
        reposition();
        visible = true;
    }

    function close() {
        visible = false;
    }

    function toggle() {
        if (visible)
            close();
        else if (Date.now() - closedAt > 250)
            open();
    }

    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    screen: anchor.item && bar ? screenOf(anchor.item) : null
    width: surface.implicitWidth
    height: surface.implicitHeight
    visible: false

    // Closing without a click inside: a press in the panel (the host reports it), or the popup
    // losing activation because the user went to another window.
    property bool wasActive: false

    // The content can grow after the window opened (a list that fills in, a label that changes), and
    // the window grows to the right and down from its corner, so place it again for the final size.
    onWidthChanged: if (visible) reposition()
    onHeightChanged: if (visible) reposition()

    onActiveChanged: {
        if (active)
            wasActive = true;
        else if (visible && wasActive)
            close();
    }

    Connections {
        target: popup.shellWindow ? popup.shellWindow.host : null
        function onWindowPressed() {
            if (popup.visible)
                popup.close();
        }
    }

    onVisibleChanged: {
        if (visible) {
            wasActive = false;
            // Opened by binding (`visible: true`) rather than open(): place it as open() would.
            if (anchor.item && bar)
                reposition();
            if (shellWindow && shellWindow.host)
                shellWindow.host.decorate(popup, shellWindow.backdrop);
            requestActivate();
            opened();
        } else {
            closedAt = Date.now();
            closed();
        }
    }

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
