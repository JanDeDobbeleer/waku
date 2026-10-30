pragma Singleton
import QtQuick

// Quickshell.Services.UPower's PowerProfile.
QtObject {
    enum Profile {
        PowerSaver = 0,
        Balanced = 1,
        Performance = 2
    }

    function toString(profile) {
        const names = ["PowerSaver", "Balanced", "Performance"];
        return names[profile] !== undefined ? names[profile] : "Invalid";
    }
}
