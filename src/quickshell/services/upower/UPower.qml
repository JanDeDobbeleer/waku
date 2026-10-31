pragma Singleton
import QtQuick
import wabar as W
import wabar.services as S

// Quickshell.Services.UPower's `UPower` singleton over the battery service.
//
//   displayDevice  the battery as one device. `percentage` is a fraction, 0 to 1 (the Quickshell
//                  docs define it as energy / energyCapacity); isPresent is false without a
//                  battery. Windows reports no energy, rate or health, so those stay 0
//   devices        a model (with `values`) of the batteries: one, or none without a battery
//   onBattery      true on battery power
//
// `backend` is the object the values are read from (available, percent, charging, plugged,
// timeRemaining); tests replace it.
UPowerBase {
    id: root

    property QtObject backend: batterySource

    readonly property UPowerDevice displayDevice: UPowerDevice {
        id: displayDev
        nativePath: "DisplayDevice"
    }
    readonly property W.ScriptModel devices: W.ScriptModel {
        values: root.backend && root.backend.available ? [batteryDev] : []
    }

    readonly property UPowerDevice battery: UPowerDevice {
        id: batteryDev
        nativePath: "battery_BAT0"
    }

    readonly property S.Battery batterySource: S.Battery {
        interval: 10000
    }

    function stateFor(source) {
        if (!source.available)
            return UPowerDeviceState.Unknown;
        if (source.charging)
            return UPowerDeviceState.Charging;
        if (source.plugged)
            return source.percent >= 100 ? UPowerDeviceState.FullyCharged : UPowerDeviceState.PendingCharge;
        return UPowerDeviceState.Discharging;
    }

    function apply(device) {
        const b = root.backend;
        device.ready = true;
        device.isPresent = b.available;
        device.type = UPowerDeviceType.Battery;
        device.powerSupply = true;
        device.percentage = b.available ? b.percent / 100 : 0;
        device.state = stateFor(b);
        device.timeToEmpty = b.available && !b.plugged && b.timeRemaining > 0 ? b.timeRemaining : 0;
        device.timeToFull = 0;
        device.iconName = !b.available ? "" : "battery-level-" + Math.round(b.percent / 10) * 10 + (b.charging ? "-charging-symbolic" : "-symbolic");
    }

    function refresh() {
        if (!root.backend)
            return;
        setOnBattery(root.backend.available && !root.backend.plugged && !root.backend.charging);
        apply(displayDev);
        apply(batteryDev);
    }

    // The service signals every sample through `updated`; a replaced backend is read at once.
    readonly property Connections watch: Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onUpdated() {
            root.refresh();
        }
    }

    onBackendChanged: refresh()
    Component.onCompleted: refresh()
}
