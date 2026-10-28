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
//             anchorItem: pill
//             Text { text: "hello"; color: Theme.text }
//         }
//     }
//
// It opens just outside the bar (below a top bar, above a bottom bar), centred on the widget and
// kept inside the screen. A click in the bar, a click in another window, Escape or `close()` closes
// it. Whatever you put inside
// is stacked in a column and sizes the window; colours come from Theme (popupSurface,
// popupBorder).
Window {
    id: popup

    // The widget the popup belongs to.
    property Item anchorItem: null
    property real gap: 6
    property real margin: 8
    property int padding: Theme.padding
    property real spacing: 6

    default property alias content: column.data

    signal opened
    signal closed

    // The bar window the widget is in.
    readonly property var bar: anchorItem ? anchorItem.Window.window : null
    // When it last closed, so that the click that closes it (a press outside, on the widget) does
    // not reopen it through the widget's own click handler.
    property double closedAt: 0

    function reposition() {
        if (!anchorItem || !bar)
            return;
        const origin = anchorItem.mapToGlobal(0, 0);
        const screenInfo = bar.screen;
        const edge = bar.spec && bar.spec.position === Bar.Bottom ? PopupPlacement.Above : PopupPlacement.Below;
        const spot = PopupPlacement.position(Qt.rect(origin.x, bar.y, anchorItem.width, bar.height),
                                             Qt.size(width, height),
                                             Qt.rect(screenInfo.virtualX, screenInfo.virtualY, screenInfo.width, screenInfo.height),
                                             edge, gap, margin);
        x = spot.x;
        y = spot.y;
    }

    function open() {
        if (!anchorItem || !bar)
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
    screen: bar ? bar.screen : null
    width: surface.implicitWidth
    height: surface.implicitHeight
    visible: false

    // Closing without a click inside: a press in the bar (the host reports it), or the popup
    // losing activation because the user went to another window.
    property bool wasActive: false

    onActiveChanged: {
        if (active)
            wasActive = true;
        else if (visible && wasActive)
            close();
    }

    Connections {
        target: popup.bar ? popup.bar.host : null
        function onBarPressed() {
            if (popup.visible)
                popup.close();
        }
    }

    onVisibleChanged: {
        if (visible) {
            wasActive = false;
            if (bar && bar.host && bar.spec)
                bar.host.decorate(popup, bar.spec.backdrop);
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
        border.width: 1
        radius: Theme.radius >= 0 ? Theme.radius : 10
        opacity: popup.visible ? 1 : 0

        Behavior on opacity {
            NumberAnimation {
                duration: 120
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
