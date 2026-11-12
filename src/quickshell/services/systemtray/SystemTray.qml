pragma ComponentBehavior: Bound
pragma Singleton
import QtQuick
import waku as W
import waku.services as S

// Quickshell.Services.SystemTray's `SystemTray` singleton over the Windows notification area.
//
//   items  a model (with `values`) of SystemTrayItem, one per icon, in arrival order. Items
//          keep their identity while the icon changes
//
// Icons come through the tray service, which is empty when the tray cannot be claimed (no live
// desktop, another tray window host, or `--no-tray`). A left click is activate(), a middle click
// secondaryActivate(), a right click display(); there is no QML menu.
//
// `tray` is the service clicks go to (click(id, button)) and `trayModel` the list of icons; tests
// replace them.
QtObject {
    id: root

    final property QtObject tray: S.Systray
    final property var trayModel: S.Systray.icons

    final property var itemList: []
    final readonly property W.ScriptModel items: W.ScriptModel {
        values: root.itemList
    }

    final readonly property Instantiator factory: Instantiator {
        model: root.trayModel
        delegate: SystemTrayItem {
            id: item
            final required property var model
            final readonly property var rowData: ({
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
