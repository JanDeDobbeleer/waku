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
    property string glyph: ""
    property string unreadGlyph: "•"

    visible: S.Notifications.available && !(hideEmpty && S.Notifications.count === 0)
    text: (S.Notifications.unread ? unreadGlyph : glyph) + (showCount ? " " + S.Notifications.count : "")
    tooltip: S.Notifications.count + (S.Notifications.count === 1 ? " notification" : " notifications")

    onClicked: panel.toggle()
    onRightClicked: S.Notifications.clear()

    PopupWindow {
        id: panel
        objectName: "notificationsPopup"
        anchorItem: root
        spacing: 8

        onOpened: S.Notifications.refresh()

        Text {
            visible: S.Notifications.items.length === 0
            color: Theme.text
            opacity: 0.6
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: "No notifications"
        }

        Repeater {
            model: S.Notifications.items

            delegate: Rectangle {
                id: card
                required property var modelData

                width: 300
                height: content.implicitHeight + 12
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
                    x: 8
                    y: 6
                    width: parent.width - 40
                    spacing: 1

                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        color: Theme.text
                        opacity: 0.6
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallFontSize
                        text: card.modelData.app + (card.modelData.time ? "  " + card.modelData.time : "")
                    }

                    Text {
                        visible: text !== ""
                        width: parent.width
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        font.bold: true
                        text: card.modelData.title
                    }

                    Text {
                        visible: text !== ""
                        width: parent.width
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: Theme.text
                        opacity: 0.8
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.smallFontSize
                        text: card.modelData.body
                    }
                }

                MouseArea {
                    objectName: "dismiss"
                    x: parent.width - 28
                    y: 4
                    width: 24
                    height: 24
                    onClicked: S.Notifications.dismiss(card.modelData.id)

                    Text {
                        anchors.centerIn: parent
                        color: Theme.text
                        opacity: 0.7
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                        text: "✕"
                    }
                }
            }
        }

        Pill {
            visible: S.Notifications.items.length > 0
            text: "Clear all"
            onClicked: S.Notifications.clear()
        }
    }
}
