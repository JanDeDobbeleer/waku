import QtQuick
import wabar as W

// Quickshell.Bluetooth's BluetoothAdapter over the Bluetooth service (the Windows radio). The
// Bluetooth singleton creates it.
//
//   enabled     writable: switches the radio on or off
//   discovering writable but without effect: the service cannot scan, so it returns to false
//   pairable, discoverable, the timeouts  held values only; Windows controls them
//   devices     a model (with `values`) of the known (paired or remembered) devices
//
// `service` is the Bluetooth service (available, radioOn, devices, setRadio, ...).
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
        pulling = false;
    }

    onServiceChanged: pull()

    onEnabledChanged: {
        if (!pulling && service)
            service.setRadio(enabled);
    }

    onDiscoveringChanged: {
        if (discovering)
            Qt.callLater(() => root.discovering = false);
    }

    readonly property Connections watch: Connections {
        target: root.service
        ignoreUnknownSignals: true
        function onChanged() {
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
