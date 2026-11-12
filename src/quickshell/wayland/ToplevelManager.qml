pragma Singleton
import QtQuick
import QtQuick.Window
import waku as W
import waku.services as S

// Quickshell.Wayland's `ToplevelManager` over the taskbar service.
//
//   toplevels        a model (with `values`) of Toplevel, one per window the taskbar lists, in
//                    arrival order; a Toplevel keeps its identity while its properties change
//   activeToplevel   the window with focus, null when it is not one of them
//
// The taskbar service tracks windows only for a model that has a window of ours, so the singleton
// keeps a 1 x 1 transparent tool window for that. It is created with the singleton, which is created
// on first use. Without a live desktop (offscreen) the taskbar service is not available and the
// list stays empty. `service` is the object the rows come from (S.TaskbarService); tests replace it.
QtObject {
    id: root

    final property var service: S.TaskbarService

    final readonly property W.ScriptModel toplevels: W.ScriptModel {
        values: root.list
    }
    final property Toplevel activeToplevel: null

    final property var list: []
    final property var cache: ({})

    final readonly property Window anchor: Window {
        visible: true
        width: 1
        height: 1
        color: "transparent"
        opacity: 0
        title: "waku-toplevel-anchor"
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    }

    final readonly property S.TaskbarModel taskbar: S.TaskbarModel {
        service: root.service
        grouping: false
        showOnlyVisible: false
        window: root.anchor
    }

    final readonly property Component toplevelComponent: Component {
        Toplevel {}
    }

    final readonly property Instantiator rows: Instantiator {
        model: root.taskbar
        delegate: QtObject {
            final required property var model
            final readonly property string key: model.key ?? ""
            final readonly property string title: model.title ?? ""
            final readonly property string exe: model.exe ?? ""
            final readonly property string className: model.className ?? ""
            final readonly property real hwnd: model.hwnd ?? 0
            final readonly property real monitorHandle: model.monitorHandle ?? 0
            final readonly property string iconUrl: model.iconUrl ?? ""
            final readonly property bool active: model.active ?? false
            final readonly property bool minimized: model.minimized ?? false
            final readonly property string progressState: model.progressState ?? "none"
            final readonly property real progress: model.progress ?? 0
            final readonly property string signature: [key, title, exe, className, hwnd, monitorHandle, iconUrl, active, minimized, progressState, progress].join("|")
            // The delegate is its own component; `root` is the singleton's id.
            // qmllint disable unqualified
            onSignatureChanged: Qt.callLater(root.rebuild)
            // qmllint enable unqualified
        }
        onObjectAdded: Qt.callLater(root.rebuild)
        onObjectRemoved: Qt.callLater(root.rebuild)
    }

    function rebuild() {
        const seen = {};
        const out = [];
        let active = null;
        for (let i = 0; i < rows.count; i++) {
            // The Instantiator hands back a QObject; the delegate's properties are not typed for it.
            // qmllint disable missing-property
            const r = rows.objectAt(i);
            if (!r || !r.key.startsWith("w:"))
                continue;
            const id = String(r.hwnd);
            let t = cache[id];
            if (!t) {
                t = toplevelComponent.createObject(root, {
                    "taskbar": taskbar
                });
                cache[id] = t;
            }
            t.row = i;
            t.hwnd = r.hwnd;
            t.monitorHandle = r.monitorHandle;
            t.title = r.title;
            t.className = r.className;
            t.appId = r.exe.replace(/\.exe$/i, "").toLowerCase();
            t.iconUrl = r.iconUrl;
            t.minimized = r.minimized;
            t.activated = r.active;
            t.progressState = r.progressState;
            t.progress = r.progress;
            // qmllint enable missing-property
            if (t.activated)
                active = t;
            seen[id] = true;
            out.push(t);
        }
        for (const id of Object.keys(cache)) {
            if (!seen[id]) {
                cache[id].destroy();
                delete cache[id];
            }
        }
        list = out;
        activeToplevel = active;
    }
}
