# brightness

Monitor brightness for the control center (Python `BrightnessService`): DDC/CI for external panels,
the active power scheme's brightness for the internal panel. Only controllable monitors are listed.

## Pieces

- `brightness.h`: `BrightnessControl` (the seam a fake implements), `BrightnessMonitor`,
  `monitorForWindow`, and the factory `createSystemBrightness`.
- `brightness_policy.h`: pure helpers (power-setting payload, the ignore window after our own
  scheme write, the default poll period), tested without Win32.
- `win32/brightness_win.cpp`: the implementation (`SystemBrightness`).
- `testing/fake_brightness.h`: `FakeBrightness`, used by the control_center tests.

## Threads

The object lives on the GUI thread. One worker thread runs detection, DDC reads and the slider
writes (a drag coalesces to the newest value per monitor) and posts results back as queued signals.
A second thread owns a message-only window that watches the video-brightness power setting.
`stop()` joins both. The row in `docs/architecture.md` (threads) lists the owner and the stop.

## Used by

control_center only (`DEPENDS brightness`). A standalone Brightness widget would depend on this
feature as well.
