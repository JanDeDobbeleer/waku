# bluetooth

The Bluetooth radio, the paired and remembered devices with battery level, and connect and
disconnect for classic audio devices (Python `BluetoothManager`), plus the Bluetooth widget.

## Pieces

- `bluetooth.h`: value types (`BluetoothDevice`, `BluetoothStatus`), the pure classification helpers,
  `BluetoothBackend` (the seam a fake implements) and the factories.
- `win32/bluetooth_win.cpp`: BluetoothAPIs, WinRT enumeration, PnP battery property and the
  `IKsControl` audio connect. The undocumented calls are allowlisted in `docs/architecture.md`.
- `qml/bluetooth_service.*`: the `Bluetooth` QML singleton mirroring the backend snapshots.
- `qml/Bluetooth.qml`: the widget.
- `testing/fake_bluetooth_backend.h`: `FakeBackend` for the tests.

## Threads

`RealBluetoothBackend` is a `QThread` owned by the `Bluetooth` singleton. It runs the enumeration,
battery reads, radio power, device watchers and audio connect; results return as queued signals.
The destructor cancels a running connect and joins (see the threads table in
`docs/architecture.md`).

## Start-up refresh

A refresh publishes in two phases. Phase 1 lists the classic devices (about 2 ms) plus the cached LE
ones and publishes at once. Phase 2 lists the paired LE devices and publishes again. The LE listing
uses `BluetoothLEDevice::GetDeviceSelectorFromPairingState(true)`. The unfiltered `FindAllAsync()`
returns every GATT service interface (1896 objects, 10 s on a Surface Pro 11) and opening each took
95 s in total, so the bar showed no device for a minute and a half.

When the radio state cannot be read and nothing is listed, the snapshot carries `radioUnknown`. The
service keeps the last state, asks again after 1, 3, 8 and 20 s, and shows "off" only if all fail.
A radio reported Off clears the devices at once. While `changed` is observed, a 60 s timer reloads
the list as a safety net; unobserved, nothing polls. `WABAR_BT_DEBUG=1` logs each refresh (radio
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
on a merged row triggers a refresh instead of an in-place edit. `WABAR_BT_DEBUG=1` logs each entry
with its container id.

## Depends on

kernel in C++ (`ensureWinrtApartment`); `shell` for the QML `S.Shell.open` clicks.
