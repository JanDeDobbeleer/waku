import QtQml
import QtQml.Models

// One object per entry of `model` (Quickshell `Variants`), most often a window per screen:
//
//     Variants {
//         model: ScreenList.screens
//         PanelWindow {
//             required property ShellScreen modelData
//             screen: modelData
//             anchors { top: true; left: true; right: true }
//             implicitHeight: 30
//         }
//     }
//
// `delegate` (the default property) is created for each entry, with the entry in its required
// `modelData`; `instances` lists what was created. Built on Instantiator: a model change rebuilds
// every instance.
Instantiator {
    id: root

    // The objects created, in model order.
    readonly property list<QtObject> instances: priv.instances

    property QtObject priv: QtObject {
        property list<QtObject> instances: []
    }

    function refreshInstances() {
        const list = [];
        for (let i = 0; i < count; ++i) {
            const object = objectAt(i);
            if (object)
                list.push(object);
        }
        priv.instances = list;
    }

    onObjectAdded: refreshInstances()
    onObjectRemoved: refreshInstances()
}
