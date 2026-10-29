import QtQuick
import tidy

// One row of a DeviceMenu: an icon, a name with a status line, and an optional text or button at
// the right.
//
//     DeviceRow { glyph: ""; name: "Headset"; status: "Connected"; trailing: "80%"; active: true }
//
// `active` highlights the row (connected), `dimmed` fades it, `interactive` turns on the hover
// highlight and the `clicked` signal. `action` is a small button label shown at the right
// (`actionClicked`).
Rectangle {
    id: root

    property string glyph: ""
    property string name: ""
    property string status: ""
    property string trailing: ""
    property string action: ""
    property bool active: false
    property bool dimmed: false
    property bool interactive: false
    property color trailingColor: muted
    readonly property color muted: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.6)

    signal clicked
    signal actionClicked

    width: parent ? parent.width - 8 : 0
    x: 4
    height: Math.max(44 + 4, texts.implicitHeight + 12 + 4)
    color: "transparent"

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        radius: 6
        color: root.active ? Theme.fillHover : (root.interactive && rowArea.containsMouse ? Theme.fill : "transparent")
        opacity: root.dimmed ? 0.6 : 1

        MouseArea {
            id: rowArea
            anchors.fill: parent
            hoverEnabled: true
            enabled: root.interactive
            onClicked: root.clicked()
        }

        Text {
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            color: Theme.text
            font.family: Theme.iconFont
            font.pixelSize: 18
            text: root.glyph
        }

        Column {
            id: texts
            x: 12 + 28 + 12
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - x - 12 - trail.width - (trail.width > 0 ? 8 : 0)

            Text {
                width: parent.width
                elide: Text.ElideRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: 14
                font.weight: Font.DemiBold
                text: root.name
            }

            Text {
                visible: text !== ""
                width: parent.width
                elide: Text.ElideRight
                color: root.muted
                font.family: Theme.fontFamily
                font.pixelSize: 12
                text: root.status
            }
        }

        Row {
            id: trail
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Text {
                visible: text !== ""
                anchors.verticalCenter: parent.verticalCenter
                color: root.trailingColor
                font.family: Theme.fontFamily
                font.pixelSize: 12
                text: root.trailing
            }

            Rectangle {
                visible: root.action !== ""
                objectName: "rowAction"
                anchors.verticalCenter: parent.verticalCenter
                width: actionLabel.implicitWidth + 28
                height: actionLabel.implicitHeight + 12
                radius: 6
                color: actionArea.containsMouse ? Theme.fillActive : Theme.fillHover

                Text {
                    id: actionLabel
                    anchors.centerIn: parent
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                    text: root.action
                }

                MouseArea {
                    id: actionArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: root.actionClicked()
                }
            }
        }
    }
}
