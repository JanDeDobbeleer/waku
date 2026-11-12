pragma Singleton
import QtQuick
import waku as W
import waku.services as S

// Quickshell.Bluetooth's `Bluetooth` singleton over waku's Bluetooth service.
//
//   defaultAdapter  the one adapter (the Windows radio), null when the machine has none
//   adapters        a model (with `values`) of that adapter, empty without one
//   devices         a model (with `values`) of the adapter's known devices
//
// `devices` holds the known devices and, while a scan's results are fresh, the nearby unpaired ones
// (`paired` false). BluetoothAdapter.discovering scans and BluetoothDevice.pair(), cancelPair() and
// forget() pair and unpair through the service; trusting is not part of it. `service` is the object
// the values come from (S.Bluetooth); tests replace it.
QtObject {
    id: root

    final property QtObject service: S.Bluetooth

    final readonly property BluetoothAdapter defaultAdapter: service && service.available ? adapter : null
    final readonly property W.ScriptModel adapters: W.ScriptModel {
        values: root.defaultAdapter ? [root.defaultAdapter] : []
    }
    final readonly property W.ScriptModel devices: W.ScriptModel {
        values: root.defaultAdapter ? root.defaultAdapter.deviceObjects : []
    }

    final readonly property BluetoothAdapter adapter: BluetoothAdapter {
        service: root.service
    }
}
