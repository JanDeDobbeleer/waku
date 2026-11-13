# bluetooth

The Bluetooth radio, the paired and remembered devices with battery level, and connect and
disconnect for classic audio devices (Python `BluetoothManager`), plus the Bluetooth widget.

## Pieces

- `bluetooth.h`: value types (`BluetoothDevice`, `BluetoothStatus`), the pure classification helpers,
  `BluetoothBackend` (the seam a fake implements) and the factories.
- `win32/bluetooth_win.cpp`: BluetoothAPIs, WinRT enumeration, the PnP battery property, the
  worker and the backend thread. `win32/bluetooth_audio_win.{h,cpp}`: the `IKsControl` audio
  connect (Core Audio and kernel streaming, no WinRT projection). `win32/bluetooth_types_win.cpp`:
  the classification helpers of `bluetooth.h` (they read WinRT constants). The undocumented calls
  are allowlisted in `docs/architecture.md`.
- `qml/bluetooth_service.*`: the `Bluetooth` QML singleton mirroring the backend snapshots.
- `qml/Bluetooth.qml`: the widget.
- `testing/fake_bluetooth_backend.h`: `FakeBackend` for the tests.

## Threads

`RealBluetoothBackend` is a `QThread` owned by the `Bluetooth` singleton. It runs the enumeration,
battery reads, radio power, device watchers and audio connect; results return as queued signals.
The destructor cancels a running connect and does not join: it queues `shutdown()` and the
thread's quit and lets the GUI event loop delete the thread and the worker when the thread has
finished. A WinRT call in progress can take 30 s, and the destructor runs on the GUI thread during
a reload; joining froze the bar for that long (seen as a 35 s `clearSingletons()`). The process
waits up to 2 s per such thread at exit (see the threads table in `docs/architecture.md`).

## Start-up refresh

Adapter presence comes from the present Bluetooth adapter PnP interfaces, not
`BluetoothFindFirstRadio`, which omits powered-off radios. Starting with Bluetooth off still
creates the backend, so the switch can enable it and follow changes from Windows Settings.

A refresh publishes in two phases. Phase 1 lists the classic devices (about 2 ms) plus the cached LE
ones and publishes at once. Phase 2 lists the paired LE devices and publishes again. The LE listing
uses `BluetoothLEDevice::GetDeviceSelectorFromPairingState(true)`. The unfiltered `FindAllAsync()`
returns every GATT service interface (1896 objects, 10 s on a Surface Pro 11) and opening each took
95 s in total, so the bar showed no device for a minute and a half.

When the radio state cannot be read and nothing is listed, the snapshot carries `radioUnknown`. The
service keeps the last state, asks again after 1, 3, 8 and 20 s, and shows "off" only if all fail.
A radio reported Off clears the devices at once. While `changed` is observed, a 60 s timer reloads
the list as a safety net; unobserved, nothing polls. `WAKU_BT_DEBUG=1` logs each refresh (radio
result, counts, timings) to stderr.

## One row per physical device

Windows lists a dual-mode device as a classic entry and an LE entry with different addresses (an
iPhone's LE node even advertises another name). `mergeSameDevices` collapses them in `applyDevices`
and in `scan`. The key is the container id: `System.Devices.Aep.ContainerId` (then
`System.Devices.ContainerId`) of the entry's `DeviceInformation`, read in `enrichDevices` through
`containerIdFor` and cached per device id. It is documented WinRT, shared by the classic and LE nodes
of one device (checked live: both nodes of the owner's headset share one id, the LE node of the
phone shares the classic iPhone's id). Without a container id, a classic and an LE entry merge when
the case-insensitive name matches and the device type is equal or one side is Generic. Two classic
entries, or two entries with different container ids, never merge.

The row keeps the classic entry (address, name, type), `connected`, `paired` and `remembered` are
true if any entry is, the battery is the first known one (dropped when not connected), and the other
addresses go to `otherAddresses`. The device watchers still cover every address; a connection change
on a merged row triggers a refresh instead of an in-place edit. `WAKU_BT_DEBUG=1` logs each entry
with its container id.

## Scan and pairing

`Bluetooth.scan()` runs the existing inquiry on the worker thread (`scanning` is true until it
ends). The unpaired devices it finds join `devices` with `paired` false, minus any whose address or
container id is already a listed row. They stay until the next scan, a pair, or the radio going off.
Only classic devices are found: the LE listing stays limited to paired devices (see Start-up
refresh). Nothing scans on its own, and the 60 s safety net only refreshes.

`pairDevice(id)` and `forgetDevice(id)` use the documented `DeviceInformationPairing`
(`PairAsync()` and `UnpairAsync()`, default pairing, no custom ceremony handler). They run on the
worker thread and share the busy flag of connect, so only one runs at a time; the thread waits in
100 ms steps so `cancelPair()` (an atomic flag, safe from the GUI thread) or a 60 s timeout can
cancel the operation. A result that means Windows needs its own UI (a PIN, a passkey comparison, a
confirmation) sets `error` to "This device needs pairing in Windows Settings". `pairing` covers
both pair and forget. A success refreshes the list. Tests use `FakeBackend`; no test pairs, forgets
or scans real hardware.

## Connect and disconnect

`connectDevice(id)` and `disconnectDevice(id)` run on the worker thread (audio endpoint state via
`IKsControl`, polled up to 10 s) and end in `connectionFinished`. The service tracks the request in
`pending_` (address to target) and puts `connecting` / `disconnecting` in the device map until the
result arrives; a second request for the same device meanwhile is ignored. A failure sets `error`
(for example "Failed to connect device. Is it powered on and in range?") and clears when the next
request starts. `connected` always comes from the OS snapshot, never from the request. The
`Quickshell.Bluetooth` `BluetoothDevice.state` maps the flags to `Connecting` / `Disconnecting`, and
a write to `connected` is a request that snaps back to the service value.

An audio endpoint belongs to a device when its friendly name contains the device name or when one
of its connectors leads to a Bluetooth device id that holds the device address
(`audioIsConnected(..., addressInt)`). The address is needed: a row can carry the name Windows gave
the Bluetooth node ("AirPods Pro - Find My") or a placeholder ("Bluetooth 90:62:..."), neither of
which is in the endpoint names ("Headphones (AirPods Pro)"). With the name alone every request on
such a device failed at once with "no audio path". Each audio request logs one line with the
endpoint state before and after, the reason code and the time taken.
The endpoints are resolved once per request (finding them walks every endpoint's topology) and
then polled by id every 100 ms, so a request ends soon after Windows makes the endpoint active.

## Names

The Bluetooth APIs return "Bluetooth <address>" (or an empty name) for a device that is not
connected, so a row of a paired headset showed its address. `isPlaceholderBtName` recognises that,
and the backend then reads `DEVPKEY_Device_FriendlyName` of the device's PnP node
(`BTHENUM\DEV_<address>`, `BTHLE\DEV_<address>` for LE), which holds the name whether or not the
device is connected. The address stays as the last resort.

Open point: the audio connect uses undocumented one-shot KS properties and is not verified on a
real headset (see the allowlist in `docs/architecture.md`).

## Depends on

kernel in C++ (`ensureWinrtApartment`); `quickshell_io` for the QML `S.Shell.open` clicks.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- `waku.services.Bluetooth` (`src/features/bluetooth/qml/bluetooth_service.{h,cpp}`): the one Bluetooth backend of a `waku` run (radio, paired devices, battery, connect/disconnect, scan, nearby devices, pair and forget on the `RealBluetoothBackend` thread), shared by every Bluetooth widget; created by the QML engine on first use and owned by it, destroyed with it, which stops the backend thread without waiting for it. Never creates a backend when `kernel::liveDesktop("bluetooth")` is false or without an adapter (`bluetooth::hasBluetoothAdapter`). Two GUI-thread `QTimer`s: a single-shot retry (1, 3, 8, 20 s) while the backend reports an unknown radio, and a 60 s safety-net refresh that runs only while a binding or connection observes `changed` (`connectNotify`/`disconnectNotify`).
