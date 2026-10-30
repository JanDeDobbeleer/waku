pragma Singleton
import QtQuick
import wabar as W
import wabar.services as S

// Quickshell.Bluetooth's `Bluetooth` singleton over wabar's Bluetooth service.
//
//   defaultAdapter  the one adapter (the Windows radio), null when the machine has none
//   adapters        a model (with `values`) of that adapter, empty without one
//   devices         a model (with `values`) of the adapter's known devices
//
// Scanning, pairing and trusting are not part of the service: BluetoothAdapter.discovering stays
// false and BluetoothDevice.pair() does nothing. `service` is the object the values come from
// (S.Bluetooth); tests replace it.
QtObject {
    id: root

    property QtObject service: S.Bluetooth

    readonly property BluetoothAdapter defaultAdapter: service && service.available ? adapter : null
    readonly property W.ScriptModel adapters: W.ScriptModel {
        values: root.defaultAdapter ? [root.defaultAdapter] : []
    }
    readonly property W.ScriptModel devices: W.ScriptModel {
        values: root.defaultAdapter ? root.defaultAdapter.deviceObjects : []
    }

    readonly property BluetoothAdapter adapter: BluetoothAdapter {
        service: root.service
    }
}
