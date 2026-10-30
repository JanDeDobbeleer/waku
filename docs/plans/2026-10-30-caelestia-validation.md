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
| `Region` has one `shapeChanged` signal for x, y, width, height; a binding that reads its own size loops | fixed: one notify signal per property |
| No network throughput in `wabar.services.Network` | fixed in lane N2: `rxBytesPerSecond`, `txBytesPerSecond`, totals, `adapters` |
| No CPU or GPU temperature source | partly fixed: `Cpu.temperature` from the ACPI thermal zones (hottest plausible zone, a proxy); GPU stays unavailable (the Adreno driver reports 0) |
| `Notification` lacks `actions`, `hints`, `expireTimeout` signals | fixed: every documented member and signal exists, with neutral fixed values |
| Wi-Fi radio switch and VPN control are no-ops | fixed in lane N2: `setWifiEnabled`, `vpnConnections`, `connectVpn`, `disconnectVpn` |
| Qt reads `D:/...` as a URL scheme in `Image.source` | Qt behaviour; the port builds `file:///` URLs |
| `QtObject` has no default property; `Array.prototype.at` and `Qt.version` missing | Qt behaviour; the port works around them |

## Verdict

Caelestia's bar, launcher, session menu, OSD, sidebar, utilities, notifications, toasts and
dashboard run on wabar with Windows data behind them, in a config home outside this repository.
Everything caelestia-specific (tokens, palette, layout, fonts) lives in that config. The engine
carries only generic Quickshell compatibility. The remaining work is the open gaps above, a
manual click-through check, and the release gate in `2026-10-30-handoff.md`.

## Update: port fixes after the owner's first look

- **Workspace icons between the dots.** Cause: the port gave the bar placeholder windows. The windows
  now come from `komorebic state`, joined with the taskbar list. Pills (`occupiedBg`) are on. A
  regression after the later merges (icons missing again) is being fixed.
- **Sizes too large.** Windows 150% scaling made every caelestia size 1.5 times larger. The port has
  a master `scale` (default 0.75), `fontScale`, `roundingScale` and an `auto` mode in
  `config\shell-tokens.json`.
- **Flyouts clipped at the right edge.** Cause: the stand-in blobs were plain rounded rectangles that
  only touched the frame. Right, top and bottom flush panels now grow into the frame. The concave
  fillets where a panel joins the frame are still not drawn.
- **Corners and spacing.** The frame is 8 logical px thick with an 8 px inner radius, matching the
  Windows 11 corner. The matching komorebi settings (padding, work-area offset) are in
  `docs\komorebi-alignment.md` in the port and are not applied. The measured inner curve was about
  35 px, not the expected 12, and is unexplained.
