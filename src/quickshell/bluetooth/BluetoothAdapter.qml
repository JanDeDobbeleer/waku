pragma ComponentBehavior: Bound
import QtQuick
import wabar as W

// Quickshell.Bluetooth's BluetoothAdapter over the Bluetooth service (the Windows radio). The
// Bluetooth singleton creates it.
//
//   enabled     writable: switches the radio on or off
//   discovering writable: true starts one scan (the service runs an inquiry of a few seconds) and
//               the value turns false when it ends; false cannot stop a scan and snaps back
//   pairable, discoverable, the timeouts  held values only; Windows controls them
//   devices     a model (with `values`) of the known (paired or remembered) devices and the
//               nearby ones of the last scan (`paired` false)
//
// `service` is the Bluetooth service (available, radioOn, devices, scanning, setRadio, scan, ...).
QtObject {
    id: root

    property QtObject service: null

    readonly property int state: enabled ? BluetoothAdapterState.Enabled : BluetoothAdapterState.Disabled
    property bool enabled: false
    property bool pairable: true
    property bool discoverable: false
    property bool discovering: false
    readonly property string adapterId: "bluetooth"
    readonly property string name: "Bluetooth"
    readonly property string dbusPath: ""
    property int pairableTimeout: 0
    property int discoverableTimeout: 0

    property var deviceObjects: []
    readonly property W.ScriptModel devices: W.ScriptModel {
        values: root.deviceObjects
    }

    property bool pulling: false

    function pull() {
        if (!service)
            return;
        pulling = true;
        enabled = service.radioOn === true;
        discovering = service.scanning === true;
        pulling = false;
    }

    onServiceChanged: pull()

    onEnabledChanged: {
        if (!pulling && service)
            service.setRadio(enabled);
    }

    onDiscoveringChanged: {
        if (pulling || !service)
            return;
        if (discovering) {
            if (service.scanning !== true)
                service.scan();
        }
        // The value follows the service: a scan that did not start, or one that cannot be stopped,
        // puts it back.
        if ((service.scanning === true) !== discovering)
            Qt.callLater(root.pull);
    }

    readonly property Connections watch: Connections {
        target: root.service
        ignoreUnknownSignals: true
        function onChanged() {
            root.pull();
        }
        function onScanningChanged() {
            root.pull();
        }
    }

    readonly property W.ScriptModel rows: W.ScriptModel {
        values: root.service ? root.service.devices : []
        objectProp: "id"
    }

    readonly property Instantiator factory: Instantiator {
        model: root.rows
        delegate: BluetoothDevice {
            required property var modelData
            service: root.service
            adapter: root
            row: modelData
        }
        onObjectAdded: (index, object) => {
            const list = root.deviceObjects.slice();
            list.splice(index, 0, object);
            root.deviceObjects = list;
        }
        onObjectRemoved: (index, object) => {
            const list = root.deviceObjects.slice();
            list.splice(index, 1);
            root.deviceObjects = list;
        }
    }
}
