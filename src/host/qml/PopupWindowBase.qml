import QtQuick
import QtQuick.Window

// The behaviour of a popup window without any content or chrome: the anchor, the placement next to it
// (kept inside the screen), open/close/toggle, closing on a click elsewhere or Escape. PopupWindow adds the
// themed surface and a column; Quickshell.PopupWindow adds nothing, as in Quickshell.
Window {
    id: popup

    // What the popup opens next to.
    readonly property PopupAnchor anchor: PopupAnchor {}
    // The space between the popup and what it opens next to (Theme.popupGap), and the one kept to
    // the screen edge.
    property real gap: Theme.popupGap
    property real margin: 8
    // Quickshell's size properties: when set (0 or more) they size the window; unset (-1), the content does.
    property real implicitWidth: -1
    property real implicitHeight: -1
    // The size the content asks for, used while implicitWidth and implicitHeight are unset. A type that
    // adds content (PopupWindow) or measures it (Quickshell.PopupWindow) binds these.
    property real contentWidth: 0
    property real contentHeight: 0

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
    width: implicitWidth >= 0 ? implicitWidth : contentWidth
    height: implicitHeight >= 0 ? implicitHeight : contentHeight
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
}
