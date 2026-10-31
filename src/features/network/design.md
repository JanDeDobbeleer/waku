# network

Wi-Fi, Ethernet and VPN state for the bar, plus Wi-Fi scan and connect, interface throughput, the
Wi-Fi radio switch and VPN control. One QML singleton (`Network`, a polled `StatSource`) and one
widget (`Wifi.qml`).

## Layers

| Path | Role |
|---|---|
| `connectivity.h`, `win32/connectivity_win.cpp` | one connectivity sample: connection profiles through WinRT, classified into Wi-Fi, Ethernet or VPN over either (`classifyConnections`, a pure function), plus the local IP address |
| `wlan.h`, `win32/wlan_win.cpp` | `WifiBackend` and its wlanapi/WinRT implementation: scan, current connection, connect with a password or a hidden SSID, forget, auto-connect |
| `throughput.h`, `throughput.cpp`, `win32/throughput_win.cpp` | `ThroughputSource` (interface counters), `ThroughputTracker` (rate and total maths, no clock) and the `GetIfTable2` reader |
| `radio.h`, `win32/radio_win.cpp` | `WifiRadioBackend` and its `Windows.Devices.Radios` implementation: read the Wi-Fi radio state, switch it |
| `vpn.h`, `win32/vpn_win.cpp` | `VpnBackend` and its RAS implementation: list entries, dial, hang up |
| `qml/network_service.*` | `Network` (QML singleton): `NetworkSource` (injectable, so tests never touch the OS), the status properties, the scan list and the connect/disconnect invokables |
| `qml/Wifi.qml` | the widget: icon pill, network list popup, password prompt; its "open settings" clicks use `Shell` |
| `testing/fake_network_source.h` | `FakeSource`, `FakeWifiBackend`, `FakeThroughputSource`, `FakeWifiRadio` and `FakeVpn` for the service tests |

## Threads

The status sample runs on the global `QThreadPool` (one at a time) and is applied on the GUI thread.
The Wi-Fi backend (`WlanBackend`, a `QThread` created by `createWlanBackend`) starts on the first
refresh or connect and is stopped and joined in the `Network` destructor. The radio and VPN
backends run each call as a one-shot task on their own two-thread pool (`win32/network_pool.h`;
RAS dial and hang-up block a thread for seconds, which on the global pool would starve its other
users) and post results to the GUI thread; they have nothing to join. The
throughput sampler is a GUI-thread timer. No source and no backend on the offscreen platform.

## Throughput

`rxBytesPerSecond`, `txBytesPerSecond`, `rxBytesTotal`, `txBytesTotal` and `adapters` all notify
through `throughputChanged`. The sampler is a 1 s `QTimer` that runs only while something is
connected to that signal (`connectNotify` and `disconnectNotify` start and stop it), so a bar that
shows no rate reads nothing. The reader is `GetIfTable2` with the 64-bit `InOctets` and `OutOctets`
(no 32-bit wrap), loopback rows and filter-driver rows (which repeat their adapter's counters) skipped; the aggregate sums the interfaces that are up. A counter
that goes backwards counts what it shows (adapter reset); a new interface starts at a baseline; a
vanished interface drops out and its bytes stay in the totals. A pause (nobody observing) resets the
baseline and the rates, not the totals. The read is one fast IP helper call on the GUI thread.

## Wi-Fi radio

`wifiEnabled` is only what Windows reports, read at each poll. `setWifiEnabled(bool)` runs
`Radio::RequestAccessAsync` and `SetStateAsync` on a pool thread, then reads the state back. A
refusal (no permission, no radio) goes to `wifiSwitchError`; `wifiEnabled` is never set
optimistically. `wifiAvailable` is false when no Wi-Fi radio could be read.

## VPN

`vpnConnections` lists the RAS phone book entries (`name`, `connected`), refreshed at each poll.
`connectVpn(name)` (`RasGetEntryDialParams`, `RasDial`, which blocks its pool thread until the dial
ends) and `disconnectVpn(name)` (`RasHangUp`, then a wait for the handle to go) take a name from that
list and run one at a time (`vpnBusy`); a failure goes to `vpnError`. Entries without saved
credentials fail soft. `vpnActive` and `vpnName` still come from the connectivity profiles.

## Open points

- RAS lists the entries of the default phone book; VPNs added by other clients (third-party
  adapters) do not appear.
- The VPN list refreshes at the status poll interval, not on RAS notifications.

## Dependencies

`DEPENDS quickshell_io` (QML only, for `Shell`). `StatSource` is `kernel/qml/stat_source.h`. Windows import libraries:
`iphlpapi`, `rasapi32`, `wlanapi`, `ws2_32`, `windowsapp`.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- `wabar.services.Network` (`src/features/network/qml/network_service.{h,cpp}`): the one Wi-Fi/Ethernet/VPN state of a `wabar` run, shared by every Wifi widget; created by the QML engine on first use and destroyed with it. Polls on the global `QThreadPool` (one sample at a time, applied on the GUI thread); the Wi-Fi scan/connect backend and its thread (`createWlanBackend`) start on the first refresh or connect and stop in the destructor. The throughput timer runs only while a QML binding observes `throughputChanged`; the radio and VPN backends are created at the first poll. No source on the offscreen platform
