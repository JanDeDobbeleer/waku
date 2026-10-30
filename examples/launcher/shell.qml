// A launcher overlay: a centred window that is hidden until it is asked for. Run it with
//
//     wabar --config-home examples/launcher
//     wabarc --config-home examples/launcher surface toggle launcher
//
// `name` is the id `wabarc surface` and hotkeys use. With no anchors the PanelWindow is centred on
// its screen at its implicit size. `keyboardFocus: OnDemand` makes it take the keyboard focus when
// it is shown, even from a hotkey or from wabarc; Escape and a click elsewhere hide it again
// (`dismissOnEscape`, `dismissOnBlur`). `hideDelay` keeps the window up while the card fades out.
// `visible: false` means no native window exists until the first show.
import QtQuick
import wabar

ShellRoot {
    PanelWindow {
        id: launcher

        name: "launcher"
        visible: false
        implicitWidth: 640
        implicitHeight: 420
        layer: WlrLayer.Overlay
        keyboardFocus: WlrKeyboardFocus.OnDemand
        dismissOnEscape: true
        dismissOnBlur: true
        hideDelay: 160
        color: "transparent"

        onShown: {
            query.text = "";
            query.forceActiveFocus();
        }

        Rectangle {
            id: card

            objectName: "card"
            anchors.fill: parent
            anchors.margins: 12
            radius: 14
            color: Theme.popupSurface
            border.color: Theme.popupBorder
            border.width: Theme.popupBorderWidth
            opacity: launcher.visible ? 1 : 0

            Behavior on opacity {
                NumberAnimation {
                    duration: 120
                }
            }

            Rectangle {
                id: field

                x: 16
                y: 16
                width: parent.width - 32
                height: 40
                radius: 8
                color: Theme.fill

                TextInput {
                    id: query

                    objectName: "query"
                    anchors.fill: parent
                    anchors.margins: 10
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize + 2
                    clip: true
                    focus: true
                }

                Text {
                    anchors.fill: parent
                    anchors.margins: 10
                    verticalAlignment: Text.AlignVCenter
                    visible: query.text === ""
                    text: "Search"
                    color: Theme.text
                    opacity: 0.5
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize + 2
                }
            }

            ListView {
                id: results

                objectName: "results"
                x: 16
                y: field.y + field.height + 12
                width: parent.width - 32
                height: parent.height - y - 16
                clip: true
                spacing: 4
                model: ["Terminal", "Files", "Settings"].filter(function (entry) {
                    return entry.toLowerCase().indexOf(query.text.toLowerCase()) >= 0;
                })

                delegate: Rectangle {
                    required property string modelData

                    width: ListView.view.width
                    height: 36
                    radius: 8
                    color: Theme.fill

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 12
                        text: parent.modelData
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSize
                    }
                }
            }
        }
    }
}
