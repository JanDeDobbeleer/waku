import QtQuick
import wabar
import wabar.services as S

// Network state: a signal-strength glyph (Wi-Fi levels, Ethernet, disconnected) and a VPN glyph
// while a VPN is up. A click opens a list of nearby networks (name, signal, lock) with a refresh;
// choosing a network with a saved profile connects to it, other networks need a password and are
// dimmed. A middle click opens the network settings, a right click shows the alternative label
// (the network name, or the IP address on Ethernet, and the VPN name). Hovering shows the name,
// signal and IP address. Hidden when there is no network source.
//
//     Wifi { interval: 5000; altFormat: "{wifi_icon} {wifi_name} {vpn_name}"; popup: false }
//
// Formats may use {wifi_icon}, {wifi_name}, {signal}, {ip_addr}, {vpn_icon} and {vpn_name}; empty
// parts and the spaces around them are dropped. `format` and `altFormat` apply on Wi-Fi and when
// disconnected, `ethernetFormat` and `ethernetAltFormat` on Ethernet. `icons` holds the glyphs
// for disconnected and for 1 to 4 bars (below 20 %, 20, 40, 60 and 80 % and up). With
// `popup: false` a click toggles the alternative label instead. `interval` is the poll interval
// in ms for the shared Network service.
Pill {
    id: root

    property int interval: 5000
    property string format: "{wifi_icon} {vpn_icon}"
    property string altFormat: "{wifi_icon} {wifi_name} {vpn_name}"
    property string ethernetFormat: "{wifi_icon} {vpn_icon}"
    property string ethernetAltFormat: "{wifi_icon} {ip_addr} {vpn_name}"
    property var icons: ["󰤮", "󰤟", "󰤢", "󰤥", "󰤨"]
    property string ethernetIcon: ""
    property string vpnIcon: "󰦝"
    property bool popup: true
    property bool showAlt: false
    // Extra room before the VPN glyph, in pixels (Python's `.wifi-widget .vpn` padding-left minus
    // the icon spacing the row already adds).
    property int vpnPadding: 0

    readonly property string kind: S.Network.kind

    function bars(percent) {
        return percent >= 80 ? 4 : percent >= 60 ? 3 : percent >= 40 ? 2 : percent >= 20 ? 1 : 0;
    }

    function glyph() {
        if (kind === "ethernet")
            return ethernetIcon;
        return icons[kind === "wifi" ? Math.min(bars(S.Network.signalPercent), icons.length - 1) : 0];
    }

    function name() {
        return kind === "ethernet" ? "Ethernet" : kind === "wifi" ? S.Network.ssid : "Disconnected";
    }

    function render(template) {
        const parts = {
            "{wifi_icon}": "",
            "{wifi_name}": name(),
            "{signal}": kind === "wifi" ? S.Network.signalPercent : "",
            "{ip_addr}": S.Network.ipAddress,
            "{vpn_icon}": "",
            "{vpn_name}": S.Network.vpnName
        };
        let text = template;
        for (const key in parts)
            text = text.split(key).join(parts[key]);
        return text.replace(/\s+/g, " ").trim();
    }

    // The glyph a template asks for with {wifi_icon}; {vpn_icon} is drawn apart, in the accent colour.
    function glyphs(template) {
        return template.includes("{wifi_icon}") ? glyph() : "";
    }

    function details() {
        const lines = [name()];
        if (kind === "wifi")
            lines.push("Signal " + S.Network.signalPercent + "%");
        if (S.Network.ipAddress !== "")
            lines.push("IP " + S.Network.ipAddress);
        if (S.Network.vpnActive)
            lines.push("VPN " + S.Network.vpnName);
        return lines.join("\n");
    }

    visible: S.Network.available
    readonly property string template: kind === "ethernet" ? (showAlt ? ethernetAltFormat : ethernetFormat) : (showAlt ? altFormat : format)
    // Wi-Fi, Ethernet and VPN glyphs are Material Design icons Segoe lacks, so they come from the
    // proportional fallback icon font (Python's `.wifi-widget .icon`).
    iconFont: Theme.iconFontFallback
    icon: glyphs(template)
    text: render(template)
    tooltip: details()

    onClicked: popup ? panel.toggle() : showAlt = !showAlt
    onRightClicked: showAlt = !showAlt

    Binding {
        target: S.Network
        property: "interval"
        value: root.interval
    }

    // The VPN glyph: Python's `.vpn` span, accent coloured with 4 px before it.
    Text {
        objectName: "wifiVpn"
        visible: root.template.includes("{vpn_icon}") && S.Network.vpnActive
        anchors.verticalCenter: parent.verticalCenter
        leftPadding: root.vpnPadding
        color: Theme.accent
        font.family: Theme.iconFontFallback
        font.pixelSize: Theme.iconSize
        text: root.vpnIcon
    }

    TapHandler {
        parent: root
        acceptedButtons: Qt.MiddleButton
        onTapped: S.Shell.open("ms-settings:network")
    }

    DeviceMenu {
        id: panel
        objectName: "wifiPopup"
        anchorItem: root
        title: "WiFi Networks"
        busy: S.Network.scanning
        emptyText: rows.length === 0 ? (S.Network.scanning ? "Scanning..." : "No networks found") : ""
        footerText: "More Wi-Fi settings"
        model: rows

        // The scan result; until it arrives, the network the machine is on.
        readonly property var rows: S.Network.networks.length > 0 ? S.Network.networks : root.kind === "wifi" ? [{
                    "ssid": S.Network.ssid,
                    "signal": S.Network.signalPercent,
                    "secured": false,
                    "connected": true,
                    "known": true
                }] : []

        onOpened: S.Network.refresh()
        onRefreshRequested: S.Network.refresh()
        onFooterClicked: S.Shell.open("ms-settings:network-wifi")

        delegate: DeviceRow {
            required property var modelData

            objectName: "wifiNetwork"
            glyph: root.icons[Math.min(root.bars(modelData.signal), root.icons.length - 1)]
            name: modelData.ssid
            status: (modelData.connected ? "Connected" : modelData.known ? "Saved" : modelData.secured ? "" : "Open")
                    + (modelData.secured ? (modelData.connected || modelData.known ? ", " : "") + "Secured" : "")
            trailing: modelData.signal + "%"
            active: modelData.connected
            dimmed: !modelData.known && !modelData.connected
            interactive: modelData.known && !modelData.connected
            onClicked: S.Network.connectTo(modelData.ssid)
        }
    }
}
