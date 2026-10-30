pragma Singleton
import QtQuick

// Quickshell.Services.UPower's UPowerDeviceState (the values are UPower's).
QtObject {
    enum State {
        Unknown = 0,
        Charging = 1,
        Discharging = 2,
        Empty = 3,
        FullyCharged = 4,
        PendingCharge = 5,
        PendingDischarge = 6
    }

    function toString(state) {
        const names = ["Unknown", "Charging", "Discharging", "Empty", "FullyCharged", "PendingCharge", "PendingDischarge"];
        return names[state] !== undefined ? names[state] : "Invalid";
    }
}
