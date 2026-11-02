pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import waku
import waku.services as S

// The open windows of this monitor as icon buttons, and the apps you pinned. A left click focuses
// a window, minimises it when it is already in front and restores it when it is minimised; on an
// app with several windows (`grouping`) it goes to the next window. A middle click closes the
// window. A right click opens a menu to close it, close all windows of the app and to pin or
// unpin the app. Hovering shows the window title. Pinned apps are in
// %LOCALAPPDATA%\waku\taskbar_pinned.json; a pinned app that is not running
// starts when you click it. Hidden when there is nothing to show and `hideEmpty` is set.
//
//     Taskbar { iconSize: 16; grouping: true; tooltip: true; hideEmpty: true }
//
// `strictFiltering` leaves out windows that cannot be minimised; `showOnlyVisible` leaves out
// windows on another virtual desktop; `monitorExclusive` shows only the windows on the monitor
// of this bar. `ignoreTitles`, `ignoreClasses` and `ignoreProcesses` hide windows by exact title,
// window class and executable name. `showCount` draws the number of windows on a grouped button.
// There is no hover preview, no peek and no drag to reorder. On a panel docked to the left or right
// edge the buttons stack top to bottom and the button in front does not widen.
Strip {
    id: root

    property int iconSize: 16
    property bool tooltip: false
    property bool showOnlyVisible: false
    property bool strictFiltering: true
    property bool monitorExclusive: false
    property bool hideEmpty: false
    property bool grouping: false
    property bool showCount: true
    property var ignoreTitles: []
    property var ignoreClasses: []
    property var ignoreProcesses: []

    // Button box, as Python YASB's `.taskbar-widget .app-container`: `buttonPadding` is the room
    // left and right of the icon, `activePadding` the same for the window in front (the button
    // widens), `buttonBorder` the transparent border and `buttonMargin` the margin on each side,
    // so neighbouring buttons sit 2 * buttonMargin apart. Defaults give a 38 px (logical) period.
    property int buttonPadding: 8
    property real activePadding: 15.5
    property int buttonBorder: 1
    property int buttonMargin: 2
    property int buttonRadius: 12
    // The button height; Python YASB's is 27 px, one less than the other widgets.
    property int buttonHeight: 27
    // The room right of the icon on a grouped button (it holds the count), and on the grouped
    // button in front.
    property int groupedPadding: 4
    property int groupedActivePadding: 10
    // Python YASB draws no bar under a running app; set true for a small bar under the icon,
    // longer for the window in front.
    property bool indicator: false
    // Opacity of the icon of a pinned app that is not running (Python YASB does not dim it).
    property real idleOpacity: 1.0

    visible: !(hideEmpty && buttons.count === 0)
    spacing: 2 * buttonMargin
    leftPadding: buttonMargin
    rightPadding: buttonMargin
    topPadding: vertical ? buttonMargin : 0
    bottomPadding: vertical ? buttonMargin : 0

    S.TaskbarModel {
        id: buttons
        service: S.TaskbarService
        window: root.Window.window
        grouping: root.grouping
        showOnlyVisible: root.showOnlyVisible
        strictFiltering: root.strictFiltering
        monitorExclusive: root.monitorExclusive
        ignoreTitles: root.ignoreTitles
        ignoreClasses: root.ignoreClasses
        ignoreProcesses: root.ignoreProcesses
    }

    Repeater {
        model: buttons

        delegate: Item {
            id: button

            required property var model
            readonly property bool grouped: root.showCount && model.windowCount > 1
            readonly property real sidePadding: model.active && !root.vertical ? root.activePadding : root.buttonPadding
            readonly property real endPadding: grouped ? (model.active ? root.groupedActivePadding : root.groupedPadding) : sidePadding

            objectName: "taskbarButton-" + model.key
            width: root.buttonBorder * 2 + sidePadding + root.iconSize + (grouped ? badge.width + 1 : 0) + endPadding
            height: root.buttonHeight

            Behavior on width {
                NumberAnimation {
                    duration: 300
                    easing.type: Easing.OutCubic
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: Math.min(root.buttonRadius, height / 2)
                color: button.model.flashing ? Theme.warnFill : (button.model.active || button.grouped) ? Theme.barFillHover : hover.hovered ? Theme.barFillHover : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: 150
                    }
                }
            }

            Image {
                x: root.buttonBorder + button.sidePadding
                anchors.verticalCenter: parent.verticalCenter
                width: root.iconSize
                height: root.iconSize
                // The provider delivers 32 px; no sourceSize (it would shrink the icon before the
                // device pixel ratio is applied) and mipmaps for the downscale.
                smooth: true
                mipmap: true
                cache: false
                opacity: button.model.running ? (button.model.minimized ? 0.6 : 1) : root.idleOpacity
                source: button.model.iconUrl
            }

            // A running app has a small bar under its icon, longer for the window in front.
            Rectangle {
                visible: root.indicator && button.model.running
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 1
                width: button.model.active ? 12 : 5
                height: 2
                radius: 1
                color: Theme.accent
            }

            // The window count: Python's `.app-count`, a small accent badge at the top right of
            // the icon.
            Rectangle {
                id: badge
                visible: button.grouped
                x: root.buttonBorder + button.sidePadding + root.iconSize + 1
                y: (parent.height - root.iconSize) / 2 - 2
                width: Math.max(6, countText.implicitWidth + 6)
                height: 12
                radius: 6
                color: Theme.accent

                Text {
                    id: countText
                    objectName: "taskbarCount"
                    anchors.centerIn: parent
                    color: Theme.onAccent
                    font.family: Theme.fontFamily
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    text: button.model.windowCount
                }
            }

            Tooltip {
                text: root.tooltip ? button.model.tooltip : ""
            }

            HoverHandler {
                id: hover
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: buttons.activate(button.model.index)
            }

            TapHandler {
                acceptedButtons: Qt.MiddleButton
                onTapped: buttons.middleClick(button.model.index)
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: menu.toggle()
            }

            PopupWindow {
                id: menu
                objectName: "taskbarMenu"
                anchor.item: button
                spacing: 4

                Pill {
                    objectName: "taskbarMenuOpen"
                    visible: !button.model.running
                    width: 170
                    text: "Open app"
                    onClicked: {
                        menu.close();
                        buttons.launch(button.model.index);
                    }
                }

                Pill {
                    objectName: "taskbarMenuPin"
                    width: 170
                    text: button.model.pinned ? "Unpin from taskbar" : "Pin to taskbar"
                    onClicked: {
                        menu.close();
                        buttons.togglePin(button.model.index);
                    }
                }

                Pill {
                    objectName: "taskbarMenuClose"
                    visible: button.model.running
                    width: 170
                    text: "Close window"
                    onClicked: {
                        menu.close();
                        buttons.closeWindow(button.model.index);
                    }
                }

                Pill {
                    objectName: "taskbarMenuCloseAll"
                    visible: button.model.running && button.model.windowCount > 1
                    width: 170
                    text: "Close all windows"
                    onClicked: {
                        menu.close();
                        buttons.closeAll(button.model.index);
                    }
                }
            }
        }
    }
}
