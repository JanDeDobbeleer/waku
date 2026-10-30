# Caelestia port: validation result

Status: result, 2026-10-30. The port runs on wabar at engine head `9795dfb` (full check: 1044 of
1044 tests). The port is a separate GPL-3.0 repository at `D:\wabar-ports\caelestia`, head `466c32e`
plus the tour script; it is not part of this repository. Plan: `2026-10-30-caelestia-port-plan.md`.

## What ran

`tools\tour.ps1` in the port starts the whole shell once with
`wabar.exe --config-home <port> --no-tray --font-engine=directwrite`, opens each panel through
`wabarc` and captures it. The shell started with 5 windows and `error=none`. The log held no
warning or error line.

## What was seen

Each state was viewed on a real screen (3840x2560 physical, 1.5 scale) and judged against what
caelestia's own QML builds. No caelestia screenshot was available to compare pixel by pixel: its
README holds one video only.

| State | Result |
|---|---|
| Bar (left, vertical) | renders: logo, komorebi workspace pills, rotated active-window label, clock, wifi, bluetooth, battery, power. Material Symbols glyphs are real |
| Launcher | rounded panel at the bottom centre, 206 real apps with their icons, first row highlighted, search row with the `>` hint |
| Session menu | right-edge pill: logout, shutdown, the mascot gif, hibernate, reboot. The icons are caelestia's own design |
| OSD | right-edge pill with volume and brightness sliders (seen by the WP6b worker) |
| Sidebar | "Notifications" header, the dino empty state "All up to date!", utilities attached below |
| Utilities | Keep awake (real), Screen recorder ("Unavailable"), Quick toggles |
| Dashboard | four tabs. Dash shows weather (live), avatar, uptime, calendar, three resource rings and the media card with the bongocat gif. Media, Performance and Weather tabs were seen by the WP8 worker |
| Notifications and toasts | real Windows toasts reached the popup list and the sidebar groups; clear-all emptied the list; DND toasts showed (WP7 worker) |

## What did not work or was not verified

- **Input regions.** Whether the open launcher accepts input while the rest stays click-through was
  not verified: it needs real cursor movement, which no test or worker may do. The engine tests
  cover the region maths and the poller with a fake cursor. A person must check this once.
- **Stubbed on Windows, by design:** the screen recorder, VPN control, the Wi-Fi radio switch,
  CPU and GPU temperature, lyrics and the visualiser, blob deformation (plain rounded
  rectangles), tray menus, and the settings app, lock screen and area picker.
- **Units.** The dashboard shows Fahrenheit, caelestia's default. Set metric in
  `config\shell.json`.
- **Debug build.** Startup takes 40 to 70 seconds and `wabarc quit` often reports "Unknown error"
  while the engine is busy. A release build is the right one to judge speed.

## Engine gaps the port found

| Gap | Status |
|---|---|
| `ShellScreen` size looked physical | not a bug: sizes are logical (verified, tests added) |
| `import Quickshell` re-exported every `wabar.services` type | fixed in lane M2 |
| Any file write in the config home reloaded the shell | fixed in lane M2 |
| `Singleton`, `TransformWatcher`, `QtQuick.Templates`, Controls.Basic | added in lane M2 |
| WebP images could not be decoded | `qwebp` deployed in lane M3 |
| `Region` has one `shapeChanged` signal for x, y, width, height; a binding that reads its own size loops | open: add separate notify signals |
| No network throughput in `wabar.services.Network` | fixed in lane N2: `rxBytesPerSecond`, `txBytesPerSecond`, totals, `adapters` |
| No CPU or GPU temperature source | open: Windows needs a helper |
| `Notification` lacks `actions`, `hints`, `expireTimeout` signals | open: toasts have no actions on Windows |
| Wi-Fi radio switch and VPN control are no-ops | fixed in lane N2: `setWifiEnabled`, `vpnConnections`, `connectVpn`, `disconnectVpn` |
| Qt reads `D:/...` as a URL scheme in `Image.source` | Qt behaviour; the port builds `file:///` URLs |
| `QtObject` has no default property; `Array.prototype.at` and `Qt.version` missing | Qt behaviour; the port works around them |

## Verdict

Caelestia's bar, launcher, session menu, OSD, sidebar, utilities, notifications, toasts and
dashboard run on wabar with Windows data behind them, in a config home outside this repository.
Everything caelestia-specific (tokens, palette, layout, fonts) lives in that config. The engine
carries only generic Quickshell compatibility. The remaining work is the open gaps above, a
manual click-through check, and the release gate in `2026-10-30-handoff.md`.
