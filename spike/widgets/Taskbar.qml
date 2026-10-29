import QtQuick
import QtQuick.Window
import tidy
import tidy.services as S

// The open windows of this monitor as icon buttons, and the apps you pinned. A left click focuses
// a window, minimises it when it is already in front and restores it when it is minimised; on an
// app with several windows (`grouping`) it goes to the next window. A middle click closes the
// window. A right click opens a menu to close it, close all windows of the app and to pin or
// unpin the app. Hovering shows the window title. Pinned apps are the same file as the Widgets
// build's taskbar (%LOCALAPPDATA%\YASB\taskbar_pinned.json); a pinned app that is not running
// starts when you click it. Hidden when there is nothing to show and `hideEmpty` is set.
//
//     Taskbar { iconSize: 16; grouping: true; tooltip: true; hideEmpty: true }
//
// `strictFiltering` leaves out windows that cannot be minimised; `showOnlyVisible` leaves out
// windows on another virtual desktop; `monitorExclusive` shows only the windows on the monitor
// of this bar. `ignoreTitles`, `ignoreClasses` and `ignoreProcesses` hide windows by exact title,
// window class and executable name. `showCount` draws the number of windows on a grouped button.
// There is no hover preview, no peek and no drag to reorder.
Row {
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

    visible: !(hideEmpty && buttons.count === 0)
    spacing: 2

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

            objectName: "taskbarButton-" + model.key
            width: root.iconSize + 8
            height: Math.max(Theme.pillHeight, root.iconSize + 8)

            Rectangle {
                anchors.fill: parent
                radius: Theme.radius >= 0 ? Math.min(Theme.radius, height / 2) : height / 2
                color: button.model.flashing ? Theme.warnFill : button.model.active ? Theme.fillActive : hover.hovered ? Theme.fillHover : "transparent"

                Behavior on color {
                    ColorAnimation {
                        duration: 150
                    }
                }
            }

            Image {
                anchors.centerIn: parent
                width: root.iconSize
                height: root.iconSize
                // The provider delivers 32 px; no sourceSize (it would shrink the icon before the
                // device pixel ratio is applied) and mipmaps for the downscale.
                smooth: true
                mipmap: true
                cache: false
                opacity: button.model.running ? (button.model.minimized ? 0.6 : 1) : 0.75
                source: button.model.iconUrl
            }

            // A running app has a small bar under its icon, longer for the window in front.
            Rectangle {
                visible: button.model.running
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 1
                width: button.model.active ? 12 : 5
                height: 2
                radius: 1
                color: Theme.accent
            }

            Text {
                objectName: "taskbarCount"
                visible: root.showCount && button.model.windowCount > 1
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 2
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                font.bold: true
                text: button.model.windowCount
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
                anchorItem: button
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
