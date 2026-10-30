import QtQuick

// Quickshell.Bluetooth's BluetoothDevice over a device of the Bluetooth service. The adapter
// creates these; a shell does not declare one.
//
//   connected   writable: true calls connect(), false disconnect()
//   battery     0 to 1, valid when batteryAvailable
//   trusted, blocked, wakeAllowed  false and read-only in effect: Windows pairs and trusts a
//               device itself
//   pair(), cancelPair(), forget()  do nothing: pairing happens in Windows Settings
//
// `service` is the Bluetooth service (connectDevice, disconnectDevice); `row` is the device's map:
// id (address), name, connected, paired, type, batteryPercent.
QtObject {
    id: root

    property QtObject service: null
    property var row: ({})
    property QtObject adapter: null

    property string name: ""
    readonly property string deviceName: String(row.name ? row.name : "")
    readonly property string address: String(row.id ? row.id : "")
    readonly property int state: connected ? BluetoothDeviceState.Connected : BluetoothDeviceState.Disconnected
    property bool connected: false
    readonly property bool paired: row.paired === true
    readonly property bool bonded: paired
    readonly property bool pairing: false
    property bool trusted: paired
    property bool blocked: false
    property bool wakeAllowed: false
    readonly property string icon: "bluetooth"
    readonly property bool batteryAvailable: row.batteryPercent !== undefined && row.batteryPercent >= 0
    readonly property real battery: batteryAvailable ? row.batteryPercent / 100 : 0
    readonly property string dbusPath: ""

    property bool pulling: false

    onRowChanged: {
        pulling = true;
        name = deviceName;
        connected = row.connected === true;
        pulling = false;
    }

    onConnectedChanged: {
        if (pulling || !service)
            return;
        if (connected)
            service.connectDevice(address);
        else
            service.disconnectDevice(address);
    }

    function connect() {
        connected = true;
    }

    function disconnect() {
        connected = false;
    }

    function pair() {
    }

    function cancelPair() {
    }

    function forget() {
    }
}
