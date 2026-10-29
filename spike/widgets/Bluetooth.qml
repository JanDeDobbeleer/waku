import QtQuick
import tidy
import tidy.services as S

// Bluetooth radio and connected devices. A click opens a panel (radio switch, paired devices with
// connect and disconnect, battery level when known), a middle click opens the Bluetooth settings,
// a right click toggles the alternate label. Hovering lists the connected devices. Hidden when the
// machine has no Bluetooth adapter.
//
//     Bluetooth { label: "%1"; labelAlt: "%1 %2" }
//
// `label` and `labelAlt`: %1 is the state icon (`iconOff`, `iconOn`, `iconConnected`), %2 the
// connected device names joined with `separator`, %3 their count. `noDevice` replaces %2 while
// nothing is connected.
Pill {
    id: root

    property string label: "%1"
    property string labelAlt: "%1 %2"
    property string iconOff: "\ue702"
    property string iconOn: "\ue702"
    property string iconConnected: "\ue702"
    property string separator: ", "
    property string noDevice: "No devices connected"
    property bool showAlt: false

    function stateIcon() {
        if (!S.Bluetooth.radioOn)
            return iconOff;
        return S.Bluetooth.connectedCount > 0 ? iconConnected : iconOn;
    }

    function names() {
        return S.Bluetooth.connectedCount > 0 ? S.Bluetooth.connectedNames.join(separator) : noDevice;
    }

    visible: S.Bluetooth.available
    icon: stateIcon()
    text: (showAlt ? labelAlt : label).replace("%1", "").trim().replace("%2", names()).replace("%3", S.Bluetooth.connectedCount)
    tooltip: S.Bluetooth.connectedCount > 0 ? "Connected devices\n" + S.Bluetooth.connectedNames.map(n => "• " + n).join("\n") : noDevice

    onClicked: panel.toggle()
    onRightClicked: showAlt = !showAlt

    TapHandler {
        acceptedButtons: Qt.MiddleButton
        onTapped: S.Shell.open("ms-settings:bluetooth")
    }

    PopupWindow {
        id: panel
        objectName: "bluetoothPopup"
        anchorItem: root
        spacing: 8

        Row {
            spacing: 10

            Text {
                width: 200
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: "Bluetooth"
            }

            Pill {
                objectName: "bluetoothSwitch"
                text: S.Bluetooth.radioOn ? "On" : "Off"
                fill: S.Bluetooth.radioOn ? Theme.accentFill : Theme.fill
                onClicked: S.Bluetooth.setRadio(!S.Bluetooth.radioOn)
            }
        }

        Text {
            visible: S.Bluetooth.radioOn && S.Bluetooth.devices.length === 0
            color: Theme.text
            opacity: 0.6
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            text: "No paired devices"
        }

        Repeater {
            model: S.Bluetooth.devices

            delegate: Pill {
                required property var modelData

                width: 300
                maxTextWidth: 276
                interactive: modelData.canConnect
                fill: modelData.connected ? Theme.accentFill : Theme.fill
                text: modelData.name
                      + (modelData.batteryPercent >= 0 ? "  " + modelData.batteryPercent + "%" : "")
                      + (modelData.canConnect ? (modelData.connected ? "  · Disconnect" : "  · Connect") : "")
                onClicked: modelData.connected ? S.Bluetooth.disconnectDevice(modelData.id) : S.Bluetooth.connectDevice(modelData.id)
            }
        }
    }
}
