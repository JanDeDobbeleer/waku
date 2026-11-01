import QtQml
import wabar.services as S

// Quickshell.Wayland's IdleMonitor over the idle time Windows reports (DesktopState.idleSeconds,
// whole seconds since the last keyboard or mouse input).
//
//   timeout            seconds without input before `isIdle` turns true; 0 or less never idles
//   enabled            false keeps `isIdle` false
//   respectInhibitors  accepted and ignored: an IdleInhibitor holds the machine awake, it does not
//                      change the input idle time
QtObject {
    id: root

    property real timeout: 0
    property bool enabled: true
    property bool respectInhibitors: true

    readonly property bool isIdle: enabled && timeout > 0 && S.DesktopState.idleSeconds >= timeout
}
