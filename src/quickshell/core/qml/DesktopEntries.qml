pragma Singleton
import QtQml
import waku.services as S

// Quickshell's `DesktopEntries` singleton: the app catalogue as DesktopEntry objects (Start Menu
// shortcuts and Store apps, see src/features/apps/design.md). A QML file because the host unit
// cannot include a feature's headers. The DesktopEntry and DesktopAction types are waku.services
// types that `import Quickshell` re-exports (the qmldir `import waku.services` line).
QtObject {
    // A model of DesktopEntry (role modelData) with a `values` array, sorted by name.
    readonly property var applications: S.AppsService.applications

    // The entry with this id, null when there is none.
    function byId(id) {
        return S.AppsService.byId(id);
    }

    // The entry a window class or process name most likely belongs to, null when none.
    function heuristicLookup(name) {
        return S.AppsService.heuristicLookup(name);
    }
}
