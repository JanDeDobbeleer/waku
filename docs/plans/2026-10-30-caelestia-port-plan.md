# Caelestia port plan

Status: plan, 2026-10-30. The port is the validation of the Quickshell-compatible engine. Inputs:
`2026-10-30-caelestia-port-assessment.md` and `2026-10-30-handoff.md`. This file holds plan text
only. No caelestia source is in the wabar repository.

## 1. Principle: the port is configuration

The caelestia port is a user config, not part of wabar. Its tokens, palette naming, layout, fonts and
styling live only in the port. The engine offers generic Quickshell compatibility and generic
services; it carries nothing that exists for one shell (rule in `AGENTS.md`). Where caelestia expects
a name the engine does not give (for example `Colours.palette.m3primary`), the port builds it in its
own QML (here: a map built from `ColorScheme.colors`).

## 1a. Licence and location

Caelestia is GPL-3.0 (clone at commit b51fdb3, 2026-10-29). Wabar is MIT. GPL code cannot be copied
into the wabar repository without placing the combined distribution under GPL-3.0. The port
therefore lives outside this repository at `D:\wabar-ports\caelestia`. It is itself GPL-3.0 (a
derivative work): keep caelestia's `LICENSE`, add a `NOTICE` naming the upstream commit. Wabar only
loads it at run time as a config home. Nothing from the port, not even a snippet, goes into
`D:\wabar`. Engine gaps the port finds become work items under `docs/plans/`.

Caelestia's README has no screenshots, only one video. Reference frames come from that video and
from the `caelestia-dots/caelestia` README.

## 2. Config home layout

```
D:\wabar-ports\caelestia\
  LICENSE NOTICE shell.qml          shell.qml trimmed: no AreaPicker, Lock, IdleMonitors, GameMode
  config\shell.json shell-tokens.json   (upstream reads %LOCALAPPDATA%\caelestia; the port reads here)
  assets\                           google-sans-flex, bongocat.gif, dino.png, logo.svg, wallpaper.webp
  modules\qs\{components,services,utils,daemons,modules}\**   the ported tree, a generated qmldir per folder
  modules\qs\compat\                ScriptModel, PersistentProperties, SystemClock, TransformWatcher, IpcHandler (QML)
  modules\Caelestia\{Config,I18n,State,Components,Blobs,Multimedia,Models,Services,Shaders,Images}\
  modules\Quickshell\{Widgets,Hyprland,Bluetooth,Services\{UPower,Pipewire,SystemTray}}\   QML shims over wabar.services
  modules\M3Shapes\                 MaterialShape stub (Rectangle with radius)
  tools\gen-qmldir.ps1 run.ps1 shot.ps1
```

Run: `wabar.exe --config-home D:\wabar-ports\caelestia --no-tray --font-engine=directwrite`.
`qs.*` resolves because `<config home>\modules` is on the import path and every folder has a qmldir
(`pragma Singleton` files get `singleton` lines). Built-in URIs (`Quickshell`, `Quickshell.Io`)
cannot be extended from the user side, so the missing core types go in `qs.compat` and the roughly
20 importing files get one added `import qs.compat` line.

No C++ in the port. Caelestia's plugin (21k lines) splits as follows.

- **Reimplement as QML singletons (all needed for first light):** `GlobalConfig`, `TokenConfig`
  (FileView and JsonAdapter over `config\*.json`); `Config` and `Tokens` as singletons with the same
  subtrees (upstream they are attached types; per-screen overrides are dropped); `Tokens.font.*` via
  `Qt.font({family, pointSize, weight, variableAxes})`; `Tokens.anim.*` as `Easing.BezierSpline`
  arrays; `Tr` (identity); `Units`; the Config enums; `PersistentState`, `RuntimeState`; `CUtils`,
  `Toaster`, `Toast`, `Requests` (XMLHttpRequest), `CircularBuffer`, `Shaders`.
- **QML replacements:** `ButtonRow`, `WavyLine` (Canvas), `LazyListView` (ListView),
  `AnimatedRepeater`, the indicator managers, `SparklineItem`, `Visual`, `FileSystemModel`
  (FolderListModel), `AppDb` (LocalStorage or in memory).
- **Visible stubs:** `Blob*` (plain Rectangles), `RoundedMask` (MultiEffect mask), `ImageAnalyser`
  (neutral luminance, or `ColorScheme.luminance`), `Qalculator` (JS arithmetic), `VisualiserBars`,
  `Lyrics`, system stat types (thin wrappers over wabar `Cpu`, `Memory`, `Gpu`, `Disks`, `Network`),
  `SessionManager` (`ControlCenter.requestAction`).
- **Dropped:** `HyprExtras`, `HyprDevices`, `Cava`, `BeatTracker`, `AudioCollector`, `VisualSource`.

## 3. Module order for first light

| Order | Module | Linux source and its replacement |
|---|---|---|
| 1 | shell root, `StyledWindow`, drawers | `HyprlandFocusGrab` becomes close-on-inactive via `QsWindow.window.active` or a stub; fullscreen detection becomes `false` |
| 2 | bar | workspaces from `wabar.services.Workspaces` (a visible "no WM" pill without a manager); active window from `TaskbarModel`; `SystemTray` from `Systray` (menu null); UPower from `Battery`; Bluetooth, Network, audio, `DesktopState` for caps/num/layout |
| 3 | launcher | `caelestia scheme` becomes `ColorScheme`; `wl-copy`/`qalc` become `Quickshell.clipboardText` and JS; launching through `DesktopEntry.execute()`; wallpapers from `Wallpapers.files` |
| 4 | OSD | audio and brightness services |
| 5 | session | `SessionManager.exec` becomes `ControlCenter.requestAction` |
| 6 | notification list | `NotificationServer` is provided; `actions` is always empty and `expireTimeout` 0, so action buttons hide |
| 7 | dashboard | `/proc`, `/sys`, `nvidia-smi` become stat sources; weather keeps Open-Meteo through XMLHttpRequest; lyrics and visualiser stubbed |
| 8 | sidebar, toasts | as 6 |

Deferred: background (`WlrLayer.Background` is not behind the icons yet), utilities cards, nexus,
lock, area picker, window info.

## 4. Work packages

| WP | Scope | Acceptance |
|---|---|---|
| 0 | skeleton, LICENSE/NOTICE, `gen-qmldir.ps1`, `run.ps1`, `shot.ps1`, assets, trimmed `shell.qml` | an empty `ShellRoot` loads; `wabarc status` reports `error=none` |
| 1 | `Caelestia/{Config,I18n,State}`, `CUtils`, `Toaster`, `Requests`, `CircularBuffer`, `Shaders`, `config/*.json` | a smoke shell prints a token, a built font and `Tr.tr("x")` with no QML errors |
| 2 | `qs/compat/*`, `Quickshell/**`, `M3Shapes/*` | a smoke shell instantiates each shim; `UPower.displayDevice.percentage`, `SystemTray.items.values`, `Bluetooth.devices` return real data |
| 3 | `Caelestia/{Components,Blobs,Multimedia,Models,Images,Services}`, `qs/components/**` | a smoke shell shows `StyledSlider`, `CircularProgress`, `ButtonRow`, `ClipRect`, `LoadingIndicator`, a `MaterialIcon` glyph |
| 4 | `qs/{services,utils,daemons}/**` (caelestia's own service API kept verbatim) | a smoke shell binds `Hypr.activeWsId`, `Audio.volume`, `Nmcli.active`, `Time.hourStr`, `Colours.palette.m3primary`, `Notifs.list.length` with no errors |
| 5 | `qs/modules/{drawers,bar}/**` | the full shell shows the bar and border frame; a click in the empty centre reaches the desktop |
| 6 | `qs/modules/{launcher,osd,session}/**` | `wabarc shortcut launcher` opens the launcher with focus in the search field; typing filters; Enter launches |
| 7 | `qs/modules/{notifications,sidebar,utilities/toasts}/**` | a Windows toast shows in the list and in the sidebar |
| 8 | `qs/modules/dashboard/**` | the dashboard opens on all four tabs |

Order: WP0, then 1-4 in parallel, then 5-8. Each worker runs `gen-qmldir.ps1`, then `run.ps1`, and
greps the log for QML errors. Workers use separate git worktrees of the port folder.

## 5. Engine gaps expected, ranked by modules blocked

1. the `qs` import root (generated qmldirs; no engine change)
2. `ScriptModel` in `Quickshell` (14 files; `qs.compat`)
3. `PersistentProperties` (every panel; `qs.compat`, no reload persistence)
4. `IpcHandler` (13 uses; inert stub; `wabarc call` later)
5. `Quickshell.Hyprland` (`GlobalShortcut` re-export, `HyprlandFocusGrab`; shim)
6. `Quickshell.Widgets` `IconImage`, `ClippingWrapperRectangle` (shim)
7. `Quickshell.iconPath` is a stub (shim maps to `image://appicon`)
8. `SystemClock` (shim)
9. `Notification.actions`, `expireTimeout`, `hints` empty (notifications degrade)
10. `Quickshell.Services.{UPower,Pipewire,SystemTray}`, `Quickshell.Bluetooth` absent (shims)
11. `Binding { property: "WlrLayershell.keyboardFocus" }` by string name (verify, else edit)
12. `TransformWatcher` (shim)
13. `MprisPlayer.volume`, `desktopEntry` (minor)
14. `WlrLayer.Background` not behind icons; `ScreenList` ignores hotplug

Items 2, 3, 4, 5, 6, 8 and 12 are Quickshell core or `Quickshell.Widgets` types a user cannot add
to a built-in URI. If several shells need them, move them into the engine as a later lane.

## 6. Validation protocol

`shot.ps1` captures the full screen with `System.Drawing.Graphics.CopyFromScreen`, at 100% and at
the display scale, after `wabarc shortcut <name>` opens each state. States: closed shell (bar
only); bar hover (popout open); launcher empty and with text; OSD during a volume change; session;
notification list with two toasts; sidebar; dashboard on each tab; a click-through probe.

Compare with frames of the README video and the `caelestia-dots/caelestia` README: same layout,
Material rounding, the Google Sans Flex and Material Symbols fonts, a palette from `ColorScheme`,
open and close animations present.

Pass: no QML errors in the log; every listed state renders; each difference is tagged Windows
limit (blur corners, no blobs, tray menus, special workspaces), wabar gap, or port bug; and
`wabarc status` stays `error=none` across a `wabarc reload`. Fail: a state is missing, a click in
the empty region does not reach the desktop, or a difference has no explanation.
