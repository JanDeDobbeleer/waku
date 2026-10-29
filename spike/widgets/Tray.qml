import QtQuick
import QtQuick.Window
import tidy
import tidy.services as S

// The notification-area icons of other programs. Icons you pin stay in the bar; the others are
// shown after them, inline or in a popup behind a chevron button. A click reaches the program,
// hovering shows its tooltip. A click with Ctrl held pins or unpins an icon; Ctrl and a right
// click open a menu to pin, unpin and move it. Pin state and order are saved per screen. Hidden
// when there is nothing to show.
//
//     Tray { iconSize: 18; unpinnedInPopup: true; hide: ["Steam.exe"] }
//
// `hide` lists executable names whose icons are never shown. Windows' own battery, volume and
// network icons are left out unless `showSystemIcons` is set. `iconsPerRow` is the width of the
// popup grid. `collapsedText` and `expandedText` are the chevron's glyphs. `stateDir` moves the
// pin state folder away from %LOCALAPPDATA%\tidy (for tests). When the tray cannot be used the
// widget says "tray unavailable" and hovering it shows why.
Row {
    id: root

    property int iconSize: 16
    property var hide: []
    property bool showUnpinned: true
    property bool unpinnedInPopup: false
    property int iconsPerRow: 5
    property bool showSystemIcons: false
    property int pinModifier: Qt.ControlModifier
    property bool tooltip: true
    property string collapsedText: "▾"
    property string expandedText: "▴"
    property string stateDir: ""

    // Icons currently shown (not hidden by their program) on each side.
    property int shownPinned: 0
    property int shownUnpinned: 0

    function count(repeater) {
        let n = 0;
        for (let i = 0; i < repeater.count; ++i) {
            const item = repeater.itemAt(i);
            if (item && !item.hidden)
                ++n;
        }
        return n;
    }

    function recount() {
        shownPinned = count(pinnedRepeater);
        shownUnpinned = count(inlineRepeater) + count(popupRepeater);
    }

    function applyFilter() {
        S.Systray.setFilter(showSystemIcons, hide);
    }

    visible: !S.Systray.available || shownPinned > 0 || (showUnpinned && shownUnpinned > 0)
    spacing: 2

    Component.onCompleted: {
        applyFilter();
        recount();
    }
    onShowSystemIconsChanged: applyFilter()
    onHideChanged: applyFilter()

    S.TrayPins {
        id: trayPins
        stateDir: root.stateDir
        icons: S.Systray.icons
        window: root.Window.window
    }

    Pill {
        objectName: "trayUnavailable"
        visible: !S.Systray.available
        interactive: false
        anchors.verticalCenter: parent.verticalCenter
        text: "tray unavailable"
        tooltip: S.Systray.unavailableReason
    }

    Repeater {
        id: pinnedRepeater
        model: trayPins.pinned
        delegate: TrayIconButton {
            objectName: "trayIcon-" + model.id
            pins: trayPins
            iconSize: root.iconSize
            pinModifier: root.pinModifier
            showTooltip: root.tooltip
            onHiddenChanged: Qt.callLater(root.recount)
        }
        onItemAdded: Qt.callLater(root.recount)
        onItemRemoved: Qt.callLater(root.recount)
    }

    Repeater {
        id: inlineRepeater
        model: root.showUnpinned && !root.unpinnedInPopup ? trayPins.unpinned : null
        delegate: TrayIconButton {
            objectName: "trayIcon-" + model.id
            pins: trayPins
            iconSize: root.iconSize
            pinModifier: root.pinModifier
            showTooltip: root.tooltip
            onHiddenChanged: Qt.callLater(root.recount)
        }
        onItemAdded: Qt.callLater(root.recount)
        onItemRemoved: Qt.callLater(root.recount)
    }

    Pill {
        id: chevron
        objectName: "trayChevron"
        visible: root.showUnpinned && root.unpinnedInPopup && root.shownUnpinned > 0
        anchors.verticalCenter: parent.verticalCenter
        text: overflow.visible ? root.expandedText : root.collapsedText
        tooltip: "Hidden icons"

        onClicked: overflow.toggle()

        PopupWindow {
            id: overflow
            objectName: "trayOverflow"
            anchorItem: chevron
            padding: 4
            spacing: 0

            Grid {
                objectName: "trayOverflowGrid"
                columns: Math.max(1, root.iconsPerRow)
                spacing: 0

                Repeater {
                    id: popupRepeater
                    model: root.showUnpinned && root.unpinnedInPopup ? trayPins.unpinned : null
                    delegate: TrayIconButton {
                        objectName: "trayIcon-" + model.id
                        pins: trayPins
                        iconSize: root.iconSize
                        cellPadding: 12
                        cornerRadius: 6
                        pinModifier: root.pinModifier
                        showTooltip: root.tooltip
                        onHiddenChanged: Qt.callLater(root.recount)
                        onActivated: overflow.close()
                    }
                    onItemAdded: Qt.callLater(root.recount)
                    onItemRemoved: Qt.callLater(root.recount)
                }
            }
        }
    }
}
