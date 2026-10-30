pragma Singleton
import QtQuick

// Quickshell.Bluetooth's BluetoothAdapterState.
QtObject {
    enum State {
        Disabled = 0,
        Enabled = 1,
        Enabling = 2,
        Disabling = 3,
        Blocked = 4
    }

    function toString(state) {
        const names = ["Disabled", "Enabled", "Enabling", "Disabling", "Blocked"];
        return names[state] !== undefined ? names[state] : "Invalid";
    }
}
