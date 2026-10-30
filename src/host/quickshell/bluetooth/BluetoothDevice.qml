import QtQuick

// Quickshell.Bluetooth's BluetoothDevice over a device of the Bluetooth service. The adapter
// creates these; a shell does not declare one.
//
//   connected   writable: true calls connect(), false disconnect()
//   battery     0 to 1, valid when batteryAvailable
//   trusted, blocked, wakeAllowed  false and read-only in effect: Windows pairs and trusts a
//               device itself
//   pair()      pairs a nearby device with Windows' default pairing; `pairing` is true until it
//               ends. A device that needs a PIN or a confirmation fails (service.error says the
//               device needs pairing in Windows Settings)
//   cancelPair()  cancels a running pair
//   forget()    forgets a paired device
//
// `service` is the Bluetooth service (connectDevice, disconnectDevice, pairDevice, forgetDevice,
// cancelPair, pairing); `row` is the device's map: id (address), name, connected, paired, type,
// batteryPercent.
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
    readonly property bool pairing: pairingNow
    property bool trusted: paired
    property bool blocked: false
    property bool wakeAllowed: false
    readonly property string icon: "bluetooth"
    readonly property bool batteryAvailable: row.batteryPercent !== undefined && row.batteryPercent >= 0
    readonly property real battery: batteryAvailable ? row.batteryPercent / 100 : 0
    readonly property string dbusPath: ""

    property bool pulling: false
    property bool pairingNow: false

    readonly property Connections watch: Connections {
        target: root.service
        ignoreUnknownSignals: true
        function onPairingChanged() {
            if (root.service && root.service.pairing !== true)
                root.pairingNow = false;
        }
    }

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
        if (!service)
            return;
        service.pairDevice(address);
        pairingNow = service.pairing === true;
    }

    function cancelPair() {
        if (service)
            service.cancelPair();
    }

    function forget() {
        if (!service)
            return;
        service.forgetDevice(address);
        pairingNow = service.pairing === true;
    }
}
