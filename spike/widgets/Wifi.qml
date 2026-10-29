import QtQuick
import tidy
import tidy.services as S

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
            "{wifi_icon}": glyph(),
            "{wifi_name}": name(),
            "{signal}": kind === "wifi" ? S.Network.signalPercent : "",
            "{ip_addr}": S.Network.ipAddress,
            "{vpn_icon}": S.Network.vpnActive ? vpnIcon : "",
            "{vpn_name}": S.Network.vpnName
        };
        let text = template;
        for (const key in parts)
            text = text.split(key).join(parts[key]);
        return text.replace(/\s+/g, " ").trim();
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
    text: render(kind === "ethernet" ? (showAlt ? ethernetAltFormat : ethernetFormat) : (showAlt ? altFormat : format))
    tooltip: details()

    onClicked: popup ? panel.toggle() : showAlt = !showAlt
    onRightClicked: showAlt = !showAlt

    Binding {
        target: S.Network
        property: "interval"
        value: root.interval
    }

    TapHandler {
        parent: root
        acceptedButtons: Qt.MiddleButton
        onTapped: S.Shell.open("ms-settings:network")
    }

    PopupWindow {
        id: panel
        objectName: "wifiPopup"
        anchorItem: root
        spacing: 6

        onOpened: S.Network.refresh()

        Row {
            spacing: 10

            Text {
                width: 190
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.text
                opacity: 0.6
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
                text: S.Network.scanning ? "Scanning..." : "Networks"
            }

            Pill {
                objectName: "wifiRefresh"
                text: "Refresh"
                onClicked: S.Network.refresh()
            }
        }

        Repeater {
            model: S.Network.networks

            delegate: Pill {
                required property var modelData

                objectName: "wifiNetwork"
                width: 256
                maxTextWidth: 232
                interactive: modelData.known && !modelData.connected
                opacity: modelData.known || modelData.connected ? 1 : 0.6
                fill: modelData.connected ? Theme.accentFill : Theme.fill
                tooltip: modelData.known ? "" : "Needs a password"
                text: (modelData.connected ? "✓ " : "") + modelData.ssid + "  " + modelData.signal + "%" + (modelData.secured ? "  🔒" : "")
                onClicked: S.Network.connectTo(modelData.ssid)
            }
        }
    }
}
