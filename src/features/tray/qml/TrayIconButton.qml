pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import waku
import waku.services as S

// One notification-area icon, used by Tray. Not meant to be placed by hand.
//
// A left click reaches the program as a click, a double click as a double click, a middle and a
// right click as theirs. A left click with `pinModifier` held pins or unpins the icon instead and
// sends nothing to the program. `pinModifier` plus a right click opens a small menu (pin or
// unpin, move left, move right, refresh); a plain right click still goes to the program.
//
// `model` is the row of the icon (roles of TrayIconModel); `pins` is the TrayPins that owns its
// place; `activated` is emitted after a click that went to the program.
Item {
    id: root

    required property var model
    property var pins: null
    property int iconSize: 16
    property int pinModifier: Qt.ControlModifier
    property bool showTooltip: true
    // The room around the icon, and (when not negative) the corner radius; the popup grid uses more.
    property int cellPadding: 4
    property real cornerRadius: -1

    // Whether the icon is pinned, as of creation and of the last time the menu opened.
    property bool pinnedNow: pins ? pins.isPinned(model.id) : false
    readonly property bool hidden: model.hidden === true

    signal activated

    function togglePin() {
        if (pins)
            pins.setPinned(model.id, !pins.isPinned(model.id));
    }

    visible: !hidden
    width: hidden ? 0 : iconSize + 2 * cellPadding
    height: hidden ? 0 : Math.max(cellPadding > 4 ? 0 : Theme.pillHeight, iconSize + 2 * cellPadding)

    Rectangle {
        anchors.fill: parent
        radius: root.cornerRadius >= 0 ? root.cornerRadius : (Theme.radius >= 0 ? Math.min(Theme.radius, height / 2) : height / 2)
        color: hover.hovered ? (root.cornerRadius >= 0 ? Theme.fill : Theme.fillHover) : "transparent"

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
        // The provider delivers the icon at 32 px; at 150 or 200 percent that is the size drawn, at
        // 100 percent it is scaled down, so no sourceSize (it would shrink it before the device
        // pixel ratio is applied) and mipmaps for the downscale.
        smooth: true
        mipmap: true
        cache: false
        source: root.model.iconUrl
    }

    Tooltip {
        text: root.showTooltip ? root.model.tooltip : ""
    }

    HoverHandler {
        id: hover
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: Qt.NoModifier
        onTapped: {
            if (tapCount === 2) {
                S.Systray.doubleClick(root.model.id);
                // The release of the double click: the service swallows it, which also clears the
                // flag so that the next real click is not swallowed.
                S.Systray.click(root.model.id, Qt.LeftButton);
            } else {
                S.Systray.click(root.model.id, Qt.LeftButton);
            }
            root.activated();
        }
    }

    TapHandler {
        acceptedButtons: Qt.LeftButton
        acceptedModifiers: root.pinModifier
        onTapped: root.togglePin()
    }

    TapHandler {
        acceptedButtons: Qt.MiddleButton
        acceptedModifiers: Qt.NoModifier
        onTapped: {
            S.Systray.click(root.model.id, Qt.MiddleButton);
            root.activated();
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedModifiers: Qt.NoModifier
        onTapped: {
            S.Systray.click(root.model.id, Qt.RightButton);
            root.activated();
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedModifiers: root.pinModifier
        onTapped: root.toggleMenu()
    }

    // The menu is created on the first pin-modifier right click: a popup window with four Pills
    // costs memory even while hidden, and most icons never open it.
    Loader {
        id: menuLoader
        active: false
        sourceComponent: menuComponent
    }

    function toggleMenu() {
        menuLoader.active = true;
        (menuLoader.item as PopupWindow).toggle();
    }

    Component {
        id: menuComponent

        PopupWindow {
            id: menu
            objectName: "trayMenu"
            anchor.item: root
            spacing: 4

            onOpened: root.pinnedNow = root.pins ? root.pins.isPinned(root.model.id) : false

            Pill {
                objectName: "trayMenuPin"
                width: 140
                text: root.pinnedNow ? "Unpin" : "Pin to bar"
                onClicked: {
                    menu.close();
                    root.togglePin();
                }
            }

            Pill {
                objectName: "trayMenuLeft"
                width: 140
                text: "Move left"
                onClicked: {
                    menu.close();
                    if (root.pins)
                        root.pins.moveBy(root.model.id, -1);
                }
            }

            Pill {
                objectName: "trayMenuRight"
                width: 140
                text: "Move right"
                onClicked: {
                    menu.close();
                    if (root.pins)
                        root.pins.moveBy(root.model.id, 1);
                }
            }

            Pill {
                objectName: "trayMenuRefresh"
                width: 140
                text: "Refresh"
                onClicked: {
                    menu.close();
                    S.Systray.refresh();
                }
            }
        }
    }
}
