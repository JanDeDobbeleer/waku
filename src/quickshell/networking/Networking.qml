pragma Singleton
import QtQuick
import waku.services as S

// octalia-qs's `Quickshell.Networking` singleton, reduced to the Wi-Fi radio switch over waku's
// Network service. Assigning `wifiEnabled` switches the radio; the value follows what Windows reports.
//
//   wifiEnabled          the radio is on
//   wifiHardwareEnabled  always true: Windows does not report a hardware switch
//
// `service` is the object the values come from (S.Network); tests replace it.
QtObject {
    id: root

    final property var service: S.Network

    final property bool wifiEnabled: false
    final readonly property bool wifiHardwareEnabled: true

    final property bool syncing: false

    onWifiEnabledChanged: {
        if (!syncing && service && wifiEnabled !== service.wifiEnabled)
            service.setWifiEnabled(wifiEnabled);
    }

    function pull() {
        syncing = true;
        wifiEnabled = service ? service.wifiEnabled : false;
        syncing = false;
    }

    onServiceChanged: pull()
    Component.onCompleted: pull()

    final readonly property Connections follow: Connections {
        target: root.service
        function onWifiChanged() {
            root.pull();
        }
    }
}
