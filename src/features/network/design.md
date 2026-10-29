# network

Wi-Fi, Ethernet and VPN state for the bar, plus Wi-Fi scan and connect. One QML singleton
(`Network`, a polled `StatSource`) and one widget (`Wifi.qml`).

## Layers

| Path | Role |
|---|---|
| `connectivity.h`, `win32/connectivity_win.cpp` | one connectivity sample: connection profiles through WinRT, classified into Wi-Fi, Ethernet or VPN over either (`classifyConnections`, a pure function), plus the local IP address |
| `wlan.h`, `win32/wlan_win.cpp` | `WifiBackend` and its wlanapi/WinRT implementation: scan, current connection, connect with a password or a hidden SSID, forget, auto-connect |
| `qml/network_service.*` | `Network` (QML singleton): `NetworkSource` (injectable, so tests never touch the OS), the status properties, the scan list and the connect/disconnect invokables |
| `qml/Wifi.qml` | the widget: icon pill, network list popup, password prompt; its "open settings" clicks use `Shell` |
| `testing/fake_network_source.h` | `FakeSource` and `FakeWifiBackend` for the service tests |

## Threads

The status sample runs on the global `QThreadPool` (one at a time) and is applied on the GUI thread.
The Wi-Fi backend (`WlanBackend`, a `QThread` created by `createWlanBackend`) starts on the first
refresh or connect and is stopped and joined in the `Network` destructor. No source and no backend
on the offscreen platform.

## Dependencies

`DEPENDS shell` (QML only). `StatSource` is `kernel/qml/stat_source.h`. Windows import libraries:
`wlanapi`, `ws2_32`, `windowsapp`.
