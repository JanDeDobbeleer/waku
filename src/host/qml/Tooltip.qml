pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window

// Hover text for the item it is declared in.
//
//     Rectangle {
//         Tooltip { text: "what this does" }
//     }
//
// It appears after `delay` ms of hovering, just outside the panel (below a top panel, above a
// bottom one, right of a left one, left of a right one), and goes when the pointer leaves or any
// button is pressed. It never takes focus or input. Declare it as a direct child of the item to watch, not inside a Row, Column or other
// positioner (it fills its parent). A Pill has `tooltip: "..."` for this.
Item {
    id: root

    property string text: ""
    property int delay: 600
    property real gap: 6
    property real margin: 8

    readonly property Item target: parent
    readonly property var bar: target ? target.Window.window : null
    readonly property var shellWindow: target ? target.QsWindow.window : null
    readonly property bool shown: tip !== null && tip.visible

    // The tip window. It is created on the first show, not with the Tooltip: a window object costs
    // memory even while hidden, and a bar has dozens of Tooltips that are never hovered.
    property var tip: null

    function show() {
        if (!hover.hovered || text === "" || !bar)
            return;
        if (!tip)
            tip = tipComponent.createObject(root);
        reposition();
        tip.visible = true;
    }

    function hide() {
        timer.stop();
        if (tip)
            tip.visible = false;
    }

    function reposition() {
        if (!tip)
            return;
        const origin = target.mapToGlobal(0, 0);
        const screenInfo = target.Screen;
        const w = shellWindow;
        const edge = PopupPlacement.edgeForDock(w ? w.dockEdge : DockEdge.None);
        const anchorRect = PopupPlacement.anchorRect(Qt.rect(origin.x, origin.y, target.width, target.height),
                                                     Qt.rect(bar.x, bar.y, bar.width, bar.height),
                                                     w ? w.dockVertical : false);
        const spot = PopupPlacement.position(anchorRect,
                                             Qt.size(tip.width, tip.height),
                                             Qt.rect(screenInfo.virtualX, screenInfo.virtualY, screenInfo.width, screenInfo.height),
                                             edge, gap, margin);
        tip.x = spot.x;
        tip.y = spot.y;
    }

    anchors.fill: parent

    onTextChanged: {
        if (text === "")
            hide();
        else if (shown)
            reposition();
    }

    HoverHandler {
        id: hover
    }

    // Any press (a click that opens a popup, say) hides the tip.
    TapHandler {
        acceptedButtons: Qt.AllButtons
        onPressedChanged: if (pressed) root.hide()
    }

    Connections {
        target: hover
        function onHoveredChanged() {
            if (hover.hovered && root.text !== "")
                timer.restart();
            else
                root.hide();
        }
    }

    Timer {
        id: timer
        objectName: "tooltipDelay"
        interval: root.delay
        onTriggered: root.show()
    }

    Component {
        id: tipComponent

        Window {
            id: tip

            objectName: "tooltipWindow"
            flags: Qt.ToolTip | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
            color: "transparent"
            screen: {
                if (!root.target || !root.bar)
                    return null;
                const name = root.target.Screen.name;
                const list = Qt.application.screens;
                for (let i = 0; i < list.length; ++i) {
                    if (list[i].name === name)
                        return list[i];
                }
                return null;
            }
            width: box.implicitWidth
            height: box.implicitHeight
            visible: false

            onVisibleChanged: {
                if (visible && root.shellWindow && root.shellWindow.host)
                    root.shellWindow.host.decorate(tip, root.shellWindow.backdrop);
            }

            Rectangle {
                id: box

                implicitWidth: label.implicitWidth + 20
                implicitHeight: label.implicitHeight + 10
                width: tip.width
                height: tip.height
                color: Theme.popupSurface
                border.color: Theme.popupBorder
                border.width: 1
                radius: 4  // Python YASB .tooltip
                opacity: tip.visible ? 1 : 0

                Behavior on opacity {
                    NumberAnimation {
                        duration: 100
                    }
                }

                Text {
                    id: label
                    anchors.centerIn: parent
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    text: root.text
                }
            }
        }
    }
}
