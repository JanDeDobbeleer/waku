pragma Singleton
import QtQuick
import waku.services as S

// Quickshell.Services.UPower's `PowerProfiles` singleton over the Windows power modes (the
// overlays of the control center service). The modes map by name: the efficiency mode to
// PowerSaver, the null-GUID mode to Balanced, anything above to Performance.
//
//   profile               the active mode; setting it activates the matching mode, and does
//                         nothing without one (no Performance mode: hasPerformanceProfile false)
//   hasPerformanceProfile whether Windows offers a mode above Balanced
//   holds                 always empty; degradationReason is always None
//
// `backend` is the object the modes come from (powerModes, setPowerMode); tests replace it.
QtObject {
    id: root

    final property QtObject backend: S.ControlCenter

    final property int profile: PowerProfile.Balanced
    final readonly property bool hasPerformanceProfile: idFor(PowerProfile.Performance) !== ""
    final readonly property var holds: []
    final readonly property int degradationReason: PerformanceDegradationReason.None

    // True while profile follows the system, so the setter below does not write back.
    final property bool syncing: false

    function profileOf(mode) {
        if (/^0{8}-0{4}-0{4}-0{4}-0{12}$/.test(String(mode.id).replace(/[{}]/g, "")))
            return PowerProfile.Balanced;
        const name = String(mode.name).toLowerCase();
        if (name.indexOf("efficien") >= 0 || name.indexOf("saver") >= 0)
            return PowerProfile.PowerSaver;
        if (name.indexOf("balanced") >= 0)
            return PowerProfile.Balanced;
        return PowerProfile.Performance;
    }

    function idFor(wanted) {
        const modes = root.backend ? root.backend.powerModes : [];
        for (let i = modes.length - 1; i >= 0; --i) {
            if (profileOf(modes[i]) === wanted)
                return modes[i].id;
        }
        return "";
    }

    function refresh() {
        const modes = root.backend ? root.backend.powerModes : [];
        for (let i = 0; i < modes.length; ++i) {
            if (modes[i].active) {
                syncing = true;
                profile = profileOf(modes[i]);
                syncing = false;
                return;
            }
        }
    }

    onProfileChanged: {
        if (syncing)
            return;
        const id = idFor(profile);
        if (id !== "")
            backend.setPowerMode(id);
    }

    final readonly property Connections watch: Connections {
        target: root.backend
        ignoreUnknownSignals: true
        function onChanged() {
            root.refresh();
        }
    }

    onBackendChanged: refresh()
    Component.onCompleted: refresh()
}
