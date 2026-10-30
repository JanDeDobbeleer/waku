pragma Singleton
import QtQuick

// Quickshell.Services.UPower's PerformanceDegradationReason. Windows reports no reason, so
// PowerProfiles.degradationReason is always None.
QtObject {
    enum Reason {
        None = 0,
        LapDetected = 1,
        HighTemperature = 2
    }

    function toString(reason) {
        const names = ["None", "LapDetected", "HighTemperature"];
        return names[reason] !== undefined ? names[reason] : "Invalid";
    }
}
