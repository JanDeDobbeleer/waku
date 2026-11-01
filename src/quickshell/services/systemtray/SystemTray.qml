pragma ComponentBehavior: Bound
pragma Singleton
import QtQuick
import wabar as W
import wabar.services as S

// Quickshell.Services.SystemTray's `SystemTray` singleton over the Windows notification area.
//
//   items  a model (with `values`) of SystemTrayItem, one per icon, in arrival order. Items
//          keep their identity while the icon changes
//
// Icons come through the tray service, which is empty when Explorer cannot be hooked (no live
// desktop, a missing hook DLL, or `--no-tray`). A left click is activate(), a middle click
// secondaryActivate(), a right click display(); there is no QML menu.
//
// `tray` is the service clicks go to (click(id, button)) and `trayModel` the list of icons; tests
// replace them.
QtObject {
    id: root

    property QtObject tray: S.Systray
    property var trayModel: S.Systray.icons

    property var itemList: []
    readonly property W.ScriptModel items: W.ScriptModel {
        values: root.itemList
    }

    readonly property Instantiator factory: Instantiator {
        model: root.trayModel
        delegate: SystemTrayItem {
            id: item
            required property var model
            readonly property var rowData: ({
                    "id": model.id,
                    "tooltip": model.tooltip,
                    "exe": model.exe,
                    "iconUrl": model.iconUrl
                })
            onRowDataChanged: setRow(rowData)
            Component.onCompleted: setRow(rowData)
            onActivateRequested: root.tray.click(trayId, Qt.LeftButton)
            onSecondaryActivateRequested: root.tray.click(trayId, Qt.MiddleButton)
            onDisplayRequested: root.tray.click(trayId, Qt.RightButton)
        }
        onObjectAdded: (index, object) => {
            const list = root.itemList.slice();
            list.splice(index, 0, object);
            root.itemList = list;
        }
        onObjectRemoved: (index, object) => {
            const list = root.itemList.slice();
            list.splice(index, 1);
            root.itemList = list;
        }
    }
}
