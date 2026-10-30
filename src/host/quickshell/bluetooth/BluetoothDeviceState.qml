pragma Singleton
import QtQuick

// Quickshell.Bluetooth's BluetoothDeviceState.
QtObject {
    enum State {
        Disconnected = 0,
        Connected = 1,
        Disconnecting = 2,
        Connecting = 3
    }

    function toString(state) {
        const names = ["Disconnected", "Connected", "Disconnecting", "Connecting"];
        return names[state] !== undefined ? names[state] : "Invalid";
    }
}
