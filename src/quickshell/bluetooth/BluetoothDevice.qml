import QtQuick

// Quickshell.Bluetooth's BluetoothDevice over a device of the Bluetooth service. The adapter
// creates these; a shell does not declare one.
//
//   connected   writable: true calls connect(), false disconnect(). The write is a request: the
//               value reads back what Windows reports and changes only when the connection is
//               really up or down. A failure leaves it unchanged (the service sets `error`).
//   state       Connecting or Disconnecting from the request until the service reports the result,
//               else Connected or Disconnected
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
// connecting, disconnecting, batteryPercent.
QtObject {
    id: root

    final property QtObject service: null
    final property var row: ({})
    final property QtObject adapter: null

    final property string name: ""
    final readonly property string deviceName: String(row.name ? row.name : "")
    final readonly property string address: String(row.id ? row.id : "")
    final readonly property int state: {
        if (row.connecting === true)
            return BluetoothDeviceState.Connecting;
        if (row.disconnecting === true)
            return BluetoothDeviceState.Disconnecting;
        return connected ? BluetoothDeviceState.Connected : BluetoothDeviceState.Disconnected;
    }
    final property bool connected: false
    final readonly property bool paired: row.paired === true
    final readonly property bool bonded: paired
    final readonly property bool pairing: pairingNow
    final property bool trusted: paired
    final property bool blocked: false
    final property bool wakeAllowed: false
    final readonly property string icon: "bluetooth"
    final readonly property bool batteryAvailable: row.batteryPercent !== undefined && row.batteryPercent >= 0
    final readonly property real battery: batteryAvailable ? row.batteryPercent / 100 : 0
    final readonly property string dbusPath: ""

    final property bool pulling: false
    final property bool pairingNow: false

    final readonly property Connections watch: Connections {
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
        // A request, not a result: snap back to what the service reports.
        pulling = true;
        connected = row.connected === true;
        pulling = false;
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
