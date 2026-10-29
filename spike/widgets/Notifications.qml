import QtQuick
import tidy
import tidy.services as S

// The Action Center toasts. A click opens a list (app, title, body, time; click a toast to open
// it, the cross dismisses it, "Clear all" empties the list), a right click clears everything.
// Hovering shows the count. Hidden when notifications are unavailable, and with `hideEmpty`
// while there are none.
//
//     Notifications { hideEmpty: true; showCount: true }
//
// The glyph is `unreadGlyph` while there are notifications and `glyph` otherwise. With
// `showCount` the count follows the glyph.
Pill {
    id: root

    property bool hideEmpty: false
    property bool showCount: false
    property string glyph: "\ue91c"
    property string unreadGlyph: "\ued0c"

    visible: S.Notifications.available && !(hideEmpty && S.Notifications.count === 0)
    icon: S.Notifications.unread ? unreadGlyph : glyph
    text: showCount ? String(S.Notifications.count) : ""
    tooltip: S.Notifications.count + (S.Notifications.count === 1 ? " notification" : " notifications")

    onClicked: panel.toggle()
    onRightClicked: S.Notifications.clear()

    // The toasts grouped by app, apps in the order of their newest toast.
    readonly property var groups: {
        const order = [];
        const byApp = {};
        for (const item of S.Notifications.items) {
            if (!(item.app in byApp)) {
                byApp[item.app] = {
                    "app": item.app,
                    "items": []
                };
                order.push(byApp[item.app]);
            }
            byApp[item.app].items.push(item);
        }
        return order;
    }

    PopupWindow {
        id: panel
        objectName: "notificationsPopup"
        anchorItem: root
        spacing: 0

        readonly property color muted: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.6)
        readonly property real contentWidth: 336

        onOpened: S.Notifications.refresh()

        Item {
            width: panel.contentWidth
            height: 44

            Text {
                x: 4
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 16
                text: "Notifications"
            }

            Rectangle {
                objectName: "notificationsClear"
                visible: S.Notifications.items.length > 0
                x: parent.width - width
                anchors.verticalCenter: parent.verticalCenter
                width: clearLabel.implicitWidth + 24
                height: clearLabel.implicitHeight + 12
                radius: 6
                color: clearArea.containsMouse ? Theme.fillActive : Theme.fillHover

                Text {
                    id: clearLabel
                    anchors.centerIn: parent
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    text: "Clear all"
                }

                MouseArea {
                    id: clearArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: S.Notifications.clear()
                }
            }
        }

        Text {
            visible: S.Notifications.items.length === 0
            width: panel.contentWidth
            topPadding: 8
            bottomPadding: 12
            horizontalAlignment: Text.AlignHCenter
            color: panel.muted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: "No notifications"
        }

        Flickable {
            visible: S.Notifications.items.length > 0
            width: panel.contentWidth
            height: Math.min(560, contentHeight)
            contentHeight: list.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: list
                width: parent.width
                spacing: 4

                Repeater {
                    model: root.groups

                    delegate: Column {
                        id: group
                        required property var modelData

                        width: list.width
                        spacing: 4

                        Text {
                            leftPadding: 4
                            topPadding: 6
                            bottomPadding: 2
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 14
                            text: group.modelData.app
                        }

                        Repeater {
                            model: group.modelData.items

                            delegate: Rectangle {
                                id: card
                                required property var modelData

                                width: group.width
                                height: content.implicitHeight + 20
                                radius: 6
                                color: cardArea.containsMouse ? Theme.fillHover : Theme.fill

                                MouseArea {
                                    id: cardArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: {
                                        S.Notifications.activate(card.modelData.id);
                                        panel.close();
                                    }
                                }

                                Column {
                                    id: content
                                    x: 12
                                    y: 10
                                    width: parent.width - 52
                                    spacing: 3

                                    Text {
                                        visible: text !== ""
                                        color: panel.muted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 12
                                        text: card.modelData.time
                                    }

                                    Text {
                                        visible: text !== ""
                                        width: parent.width
                                        elide: Text.ElideRight
                                        color: Theme.text
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        text: card.modelData.title
                                    }

                                    Text {
                                        visible: text !== ""
                                        width: parent.width
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 3
                                        elide: Text.ElideRight
                                        color: panel.muted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: 14
                                        text: card.modelData.body
                                    }
                                }

                                MouseArea {
                                    objectName: "dismiss"
                                    x: parent.width - 32
                                    y: 6
                                    width: 24
                                    height: 24
                                    onClicked: S.Notifications.dismiss(card.modelData.id)

                                    Text {
                                        anchors.centerIn: parent
                                        color: panel.muted
                                        font.family: Theme.iconFont
                                        font.pixelSize: 12
                                        text: ""
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
