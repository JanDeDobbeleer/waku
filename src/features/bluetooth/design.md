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

## Depends on

kernel in C++ (`ensureWinrtApartment`); `shell` for the QML `S.Shell.open` clicks.
