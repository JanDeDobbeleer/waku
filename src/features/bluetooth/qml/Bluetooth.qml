pragma ComponentBehavior: Bound
import QtQuick
import wabar
import wabar.services as S

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

    // Horizontal padding on a bar: Python's `.bluetooth-widget` uses 3 px, half of the others'.
    property int barPadding: 3
    padding: flat ? barPadding : Theme.padding

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

    DeviceMenu {
        id: panel
        objectName: "bluetoothPopup"
        anchor.item: root
        title: "Bluetooth"
        buttonText: S.Bluetooth.radioOn ? "On" : "Off"
        buttonChecked: S.Bluetooth.radioOn
        sectionTitle: "Your devices"
        emptyText: S.Bluetooth.radioOn && S.Bluetooth.devices.length === 0 ? "No paired devices" : ""
        footerText: "More Bluetooth settings"
        refreshable: false
        model: S.Bluetooth.devices

        onButtonClicked: S.Bluetooth.setRadio(!S.Bluetooth.radioOn)
        onFooterClicked: S.Shell.open("ms-settings:bluetooth")

        delegate: DeviceRow {
            required property var modelData

            glyph: root.iconOn
            name: modelData.name
            status: modelData.connected ? "Connected" : "Paired"
            trailing: modelData.batteryPercent >= 0 ? modelData.batteryPercent + "%" : ""
            trailingColor: modelData.batteryPercent >= 0 && modelData.batteryPercent <= 20 ? Theme.warn : muted
            action: modelData.canConnect ? (modelData.connected ? "Disconnect" : "Connect") : ""
            active: modelData.connected
            onActionClicked: modelData.connected ? S.Bluetooth.disconnectDevice(modelData.id) : S.Bluetooth.connectDevice(modelData.id)
        }
    }
}
