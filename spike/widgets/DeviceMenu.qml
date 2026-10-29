import QtQuick
import tidy

// The popup of the Wi-Fi and Bluetooth widgets, as in Python YASB: a header with the title and a
// button, a list of rows and a footer with a settings link and a refresh button.
//
//     DeviceMenu {
//         anchorItem: pill
//         title: "Bluetooth"
//         buttonText: "On"
//         model: devices
//         delegate: DeviceRow { ... }
//     }
//
// `refreshable: false` hides the refresh button. `busy` shows the progress bar under the header. `sectionTitle` is a muted heading over the
// rows, `emptyText` shows while there are no rows. The list scrolls past `maxBodyHeight`.
PopupWindow {
    id: root

    property string title: ""
    property string buttonText: ""
    property bool buttonChecked: false
    property bool busy: false
    property bool refreshable: true
    property string sectionTitle: ""
    property string emptyText: ""
    property string footerText: ""
    property var model: []
    property Component delegate
    property real menuWidth: 375
    property real minBodyHeight: 288
    property real maxBodyHeight: 520
    readonly property color muted: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.6)
    readonly property color dialogSurface: Qt.darker(Theme.popupSurface, 1.2)
    readonly property real corner: Theme.radius >= 0 ? Theme.radius : 10
    readonly property color divider: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.07)

    signal buttonClicked
    signal refreshRequested
    signal footerClicked

    padding: 0
    spacing: 0

    Item {
        width: root.menuWidth
        height: 50
        clip: true

        Rectangle {
            width: parent.width
            height: parent.height + root.corner
            radius: root.corner
            color: root.dialogSurface
        }

        Text {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 14
            font.weight: Font.DemiBold
            text: root.title
        }

        Rectangle {
            objectName: root.objectName + "Button"
            visible: root.buttonText !== ""
            x: parent.width - width - 16
            anchors.verticalCenter: parent.verticalCenter
            width: buttonLabel.implicitWidth + 28
            height: buttonLabel.implicitHeight + 12
            radius: 6
            color: root.buttonChecked ? Theme.accent : (buttonArea.containsMouse ? Theme.fillHover : Theme.fillActive)

            Text {
                id: buttonLabel
                anchors.centerIn: parent
                color: root.buttonChecked ? Theme.surface : Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
                text: root.buttonText
            }

            MouseArea {
                id: buttonArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.buttonClicked()
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: root.divider
        }

        Rectangle {
            id: progress
            visible: root.busy
            anchors.bottom: parent.bottom
            width: parent.width / 4
            height: 2
            color: Theme.accent

            SequentialAnimation on x {
                running: root.busy && root.visible
                loops: Animation.Infinite
                NumberAnimation {
                    from: -progress.width
                    to: root.menuWidth
                    duration: 1400
                }
            }
        }
    }

    Flickable {
        id: body
        width: root.menuWidth
        height: Math.min(root.maxBodyHeight, Math.max(root.minBodyHeight, contentHeight))
        contentHeight: list.implicitHeight + 8
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: list
            x: 0
            y: 4
            width: parent.width
            spacing: 0

            Text {
                visible: root.sectionTitle !== ""
                leftPadding: 12
                topPadding: 10
                bottomPadding: 4
                color: root.muted
                font.family: Theme.fontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
                text: root.sectionTitle
            }

            Text {
                objectName: root.objectName + "Empty"
                visible: root.emptyText !== ""
                leftPadding: 12
                topPadding: 6
                color: root.muted
                font.family: Theme.fontFamily
                font.pixelSize: 13
                text: root.emptyText
            }

            Repeater {
                model: root.model
                delegate: root.delegate
            }
        }
    }

    Item {
        width: root.menuWidth
        height: 43
        clip: true

        Rectangle {
            y: -root.corner
            width: parent.width
            height: parent.height + root.corner
            radius: root.corner
            color: root.dialogSurface
        }

        Rectangle {
            width: parent.width
            height: 1
            color: root.divider
        }

        Text {
            objectName: root.objectName + "Footer"
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            color: footerArea.containsMouse ? Theme.text : Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.85)
            font.family: Theme.fontFamily
            font.pixelSize: 13
            text: root.footerText

            MouseArea {
                id: footerArea
                anchors.fill: parent
                anchors.margins: -4
                hoverEnabled: true
                onClicked: root.footerClicked()
            }
        }

        Rectangle {
            visible: root.refreshable
            x: parent.width - width - 12
            anchors.verticalCenter: parent.verticalCenter
            width: 26
            height: 26
            radius: 4
            color: refreshArea.containsMouse ? Theme.fillActive : "transparent"

            Text {
                anchors.centerIn: parent
                color: Theme.text
                font.family: Theme.iconFont
                font.pixelSize: 16
                text: ""
            }

            MouseArea {
                id: refreshArea
                objectName: root.objectName + "Refresh"
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.refreshRequested()
            }
        }
    }
}
