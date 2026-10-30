# Can wabar host caelestia? A port assessment

Status: assessment, 2026-10-30. Nothing here is implemented. Branch `perf/lazy-surfaces`.
Input: `2026-10-29-wabar-extensibility-proposal.md` (the plan) and a read-only clone of
https://github.com/caelestia-dots/shell (282 QML files, about 35,000 lines, Quickshell v0.3.0 API).
Quickshell names below were checked by the investigating agent against quickshell.org type pages and
caelestia's own usage. Counts are rough.

## 1. Goal

Users can port the supported functionality and styling of existing Quickshell shells to Windows with
wabar. Caelestia is the first reference. The plan must therefore be Quickshell-compatible in names
and semantics, not only in ideas.

## 2. Verdict

About 40% of caelestia ports under the plan as written. About 65-70% ports with the changes in
section 5. The ceiling on Windows is about 75%. The ceiling comes from what Windows lacks: a
session-lock protocol with PAM, being the notification daemon, DBus tray menus rendered in QML,
Wayland screen capture, and compositor blur that honours per-pixel alpha.

Top blockers:

1. **Input regions.** Caelestia opens one full-screen `PanelWindow` per screen
   (`modules/drawers/ContentWindow.qml`, anchored on all four edges, `exclusionMode: Ignore`) and
   draws the bar, launcher, dashboard, sidebar, session menu, OSD, notifications and popouts as items
   inside it. A composed `Region` (`mask:` used 27 times, `Region` 10 times, `PopupWindow` 0 times)
   makes everything except the bar and the open panels click-through. The plan offers no input region
   in 2.0 (section 5 there: `SetWindowRgn` breaks DWM effects).
2. **Its own C++ plugin.** About 20,800 lines, 68 `.cpp`, 14 singletons (`Caelestia.Config`,
   `I18n`, `State`, `Components`, `Blobs`, `Shaders`, `Toaster`, `Requests` and more). Only 15 files
   are Linux-bound (pipewire, aubio, cava, `/proc`, `/sys`, logind, the Hyprland socket,
   libqalculate). The plan's non-goal "no plugin ABI" stops the other 70% from loading.
3. **Names.** The plan keeps Quickshell's ideas but renames most types and properties, so ported QML
   fails on its first line even where the meaning matches. The list is in section 6.

Runtime dependencies from `nix/default.nix` that need a Windows answer: hyprland, caelestia-cli
(Python: wallpaper and Material 3 scheme generation), networkmanager, ddcutil, brightnessctl,
lm_sensors, swappy, wl-clipboard, libqalculate, fish, aubio, libcava, pipewire.

## 3. Feature table

Status: **C** covered by the plan, **C+** covered with a change to the plan, **P** possible on
Windows but not planned, **X** impossible or meaningless on Windows.

| Quickshell feature | caelestia use | Status | wabar action |
|---|---|---|---|
| `PanelWindow` (`anchors.top/bottom/left/right: bool`, `margins`, `exclusiveZone`, `exclusionMode`, `aboveWindows`, `focusable`, `screen`, `color`, `implicitWidth/Height`) | 3 direct, all through `StyledWindow`; full-screen drawers, background, four 1 px `ExclusionZone` windows | C+ | `Surface`, `Panel` and `Overlay` match in mechanism, not in names. Adopt Quickshell's. One-pixel exclusion windows work as app bars with `input: PassThrough` |
| `WlrLayershell.layer/keyboardFocus/namespace/exclusionMode` | 10; `Background`, `Top`/`Overlay` by fullscreen, `OnDemand` per panel state, `Exclusive` for the area picker | C+ / X | `layer` and `focus` exist in the plan; expose them under a `Quickshell.Wayland` shim. `Background` is WorkerW (see 5.8). `Exclusive` keyboard: rejected by the plan, correctly |
| `mask: Region` (`Intersection.Xor/Subtract`) | 27 / 10; the core of the drawers design | P | Spike S7 (5.2) |
| `Variants { model: Quickshell.screens }`, `ShellScreen` | 15 | C+ | `screens: [...]` is less general. Ship `Variants` over `Instantiator`, and `Desktop.screens` with `name`, `width`, `height`, `x`, `y`, `model`, `serialNumber` |
| `Scope`, `ShellRoot`, `LazyLoader { loading }`, `FloatingWindow` | 11, 2, 5, 2 | C+ | QML-only shims, S |
| `QsWindow.window` attached | 16 | C+ | Attached `QsWindow` with `window`, `contentItem`, `itemPosition`, `mapFromItem` |
| `HyprlandFocusGrab` | 2 | C, partly | `dismissOnBlur` on an activated overlay. A no-focus panel cannot see outside clicks |
| `PopupWindow { anchor.item/rect/window/edges/gravity }` | 0 | C | Keep, named `anchor.item`, not `anchorItem` |
| `GlobalShortcut { name, appid, description }` | 23 | C+ | `Hotkey` gains `name`, `description`; `keys` optional; `wabarc shortcut <name>` fires it, so whkd or AutoHotkey bind it as Hyprland does |
| `IpcHandler { target; function f(a: type): type }` | 13 | C | Same shape. Keep |
| `PersistentProperties { reloadableId }` | 6 | C+ | Rename `Persist`, key by `reloadableId` (S) |
| `Process` (`command`, `running`, `stdout: StdioCollector/SplitParser`, `stderr`, `environment`, `workingDirectory`, `exec()`, `exited(exitCode, exitStatus)`) | 34 | C+ | Adopt Quickshell's shape, not `stream`/`line()`/`json` |
| `Quickshell.execDetached/env/shellPath/shellDir/iconPath/clipboardText/processId` | 24 / 15 / 9 / 19 / 2 / 1 | C+ | A `Quickshell` compat singleton over `Shell` |
| `FileView` (`path`, `watchChanges`, `onLoaded`, `onLoadFailed`, `setText`, `adapter: JsonAdapter`) | 12 | P | Not in the plan. C++ type, M |
| `Quickshell.Widgets` (`IconImage`, `ClippingWrapperRectangle`, `WrapperMouseArea`) | 8, 2, 1 | P | Pure Qt Quick shims, S |
| `DesktopEntries`, `DesktopEntry.execute()` | 12 | C+ | The planned `Apps` feature, in Quickshell's shape |
| `Quickshell.Services.Pipewire` | 11 + 17 `PwNode` | C, partly | `AudioOutput`/`AudioInput` cover devices, volume, mute. Per-app streams need `IAudioSessionManager2` (M) |
| `CavaProvider`, `AudioCollector`, `BeatTracker` | visualiser | P | WASAPI loopback plus libcava and aubio. L, low value |
| `Mpris` | 4 + 6 | C, partly | `Media` has one session. Add a `players` list (GSMTC has all sessions), S/M |
| `UPower`, `PowerProfiles` | 36 + 7 | C | `Battery`, `ControlCenter` power modes |
| `NotificationServer` | 1 server, about 100 field reads | X (daemon) / C (list) | Windows owns toasts. The `Notifications` listener gives summary, body, app, image. Popups and `actions.invoke` stay with Windows |
| `SystemTray` (`menu` via `QsMenuOpener`) | 6 + `TrayMenu.qml` | C / X | The tray hook covers icons and clicks. A QML menu is impossible (the app owns its Win32 menu). `menu` is always null |
| `Quickshell.Bluetooth` | 32 | C, partly | Discovery and pairing not exposed (M) |
| `Quickshell.Hyprland` (workspaces, monitors, toplevels, dispatch, special workspaces, caps lock, layout) | 10 imports | C / X | `Workspaces` plus `S.Windows` plus `Desktop.foregroundFullscreen`. Special workspaces and `dispatch` are meaningless. Caps/num lock and layout: `GetKeyState`, `GetKeyboardLayout` (S) |
| `Nmcli.qml`, `VPN.qml` | 2 services | C | `Network` replaces them; they are not ported |
| `Brightness.qml` | 1 | C | `Brightness` (DDC/CI and WinRT) |
| `SessionManager` | 1 | C | `ControlCenter` session actions |
| `cpu`, `memory`, `gpu`, `storage`, `networkusage` | dashboard | C / P | GPU (PDH, DXGI) and disk not planned, S each |
| `Colours.qml`, `caelestia scheme`, `ImageAnalyser` (Material 3 from wallpaper) | 42 imports, palette everywhere | P | A pure Material 3 tonal-palette generator, M (5.6) |
| `Wallpapers.qml`, video wallpaper | background module | C / P | `Wallpapers` exists. A window behind the icons needs WorkerW (5.8) |
| `WlSessionLock`, `PamContext` | `modules/lock`, 2,900 lines | X | `LockWorkStation()` only. Drop the module |
| `ScreencopyView`, recording, `swappy` | 5 + 2 modules | P | `Windows.Graphics.Capture`, DWM thumbnails. L, last |
| `IdleInhibitor`, idle monitors | 2 | P | `SetThreadExecutionState`, `GetLastInputInfo`. S |
| `Qalculator`, `Lyrics`, `Weather`, `Requests` | launcher, dashboard | P / C | The plan's JS calculator is saner than libqalculate. Network ones are portable |
| `M3Shapes`, `QtQuick.Shapes`, `QtQuick.Effects`, custom `.frag` via ShaderTools, `QtQuick.Controls` | everywhere | C | All Qt, all run on the Windows RHI. `deploy` must ship Controls Basic and the `.qsb` files. `M3Shapes` is a module to vendor |

## 4. Styling portability

- **Fonts.** Google Sans Flex loads with `FontLoader`; Material Symbols Rounded, Rubik and
  CaskaydiaCove NF are system fonts the user installs. Variable-font axes work on Qt 6.10's
  DirectWrite backend. Wabar defaults to the GDI font engine (`src/app/wabar/main.cpp`), so a shell
  that needs variable axes must opt in. Portable.
- **Rounded shapes, animation, elevation.** `QtQuick.Shapes`, `M3Shapes`, `RectangularShadow` and the
  SDF blob materials are pure Qt Quick. Portable.
- **Blur and transparency.** Caelestia asks Hyprland for `layerrule blur` with `ignore_alpha`, so
  blur follows opacity. Windows blurs the whole window rectangle (accent, Acrylic, Mica) and has no
  alpha threshold. With one window per panel each panel gets its own backdrop, which suits
  rectangular panels. A rounded blurred panel gets Windows' fixed corner radius or blurred corners;
  caelestia's large radii cannot be honoured with blur. Opaque tinted mode ports fully.
- **Input regions.** See blocker 1. Visual rounding needs no mask; only input does.

## 5. Plan changes, in priority order

Sizes: S under 300 changed lines, M 300 to 1,000, L over 1,000.

1. **Quickshell window vocabulary (M; decide before migration step 1).** `PanelWindow` becomes the
   creatable type with Quickshell's names: `anchors.{top,bottom,left,right}: bool`, `margins`,
   `exclusiveZone: int`, `exclusionMode: ExclusionMode.{Auto,Ignore,Normal}`, `aboveWindows`,
   `focusable`, `screen: ShellScreen`, `color`, `mask: Region`, `implicitWidth/Height`, `visible`,
   plus wabar's `layer` and `keyboardFocus`. `Panel` and `Overlay` stay as presets. Add the attached
   `QsWindow`, `Variants`, `Scope`, `ShellRoot`, `LazyLoader`, `FloatingWindow`, `Region`,
   `ShellScreen`. A thin `Quickshell.Wayland` module exposes `WlrLayershell.layer`, `keyboardFocus`,
   `namespace`, `exclusionMode` as attached properties over the same engine state, so caelestia's
   `StyledWindow.qml` loads unchanged. The engine work equals the plan's `Surface`; only names move.
2. **`mask: Region` (M plus spike S7).** No `SetWindowRgn`. While a masked window is shown, poll
   `GetCursorPos` at 30 Hz, test the point against the region, and toggle `WS_EX_TRANSPARENT`.
   Measure click loss at region edges. If it is unacceptable, document "one window per panel" and give
   hover zones as thin edge windows.
3. **`Quickshell.Io` and the `Quickshell` singleton (M).** `FileView` with `JsonAdapter` and
   `FileViewError`; `Process` in Quickshell's shape (`command`, `running`, `stdout`/`stderr` parsers
   `StdioCollector` and `SplitParser`, `environment`, `workingDirectory`, `stdinEnabled`, `write`,
   `exec`, `startDetached`, `exited(exitCode, exitStatus)`); `ElapsedTimer`; the singleton members
   `env`, `execDetached`, `shellDir`, `shellPath`, `statePath`, `cachePath`, `dataPath`,
   `clipboardText`, `processId`, `iconPath`, `reload`.
4. **`GlobalShortcut`-compatible `Hotkey` (S).** Properties `name`, `description`, optional `keys`;
   `wabarc shortcut <name>` triggers it.
5. **Extension plugins from the config home (M).** Add `<config home>/modules` and `--import-path`
   to the QML import path so a user-built Qt QML plugin loads, with a Qt version and build id check
   and a note in `SECURITY.md`. This unlocks about 70% of caelestia's plugin with no engine code.
   Amend the non-goal "no plugin ABI" to "no ABI promise".
6. **Material 3 dynamic colour (M).** A pure, tested port of material-color-utilities: tonal palettes
   from an image, plus luminance. It is what `caelestia scheme` does and what 233 `Config` and
   `Colours` reads depend on.
7. **Service gaps (S each, M together).** `Media.players`; per-app audio streams; Bluetooth discovery
   and pairing; `Desktop.capsLock`, `numLock`, `keyboardLayout`, `idleSeconds`, an idle inhibitor; GPU
   and disk stat sources; `Notifications` fields `appName`, `summary`, `body`, `image`, `urgency` on
   `toastAdded`.
8. **Reopen `layer: Background` (S to decide, M to do).** Caelestia's background module (wallpaper,
   visualiser, desktop clock) needs a window behind the icons. The WorkerW attach exists and is
   allowlisted. Offer it with the documented Explorer-restart fallback.
9. **Capture (L, last).** `ScreencopyView` over `Windows.Graphics.Capture`; window previews over DWM
   thumbnails; WASAPI loopback for `AudioCollector`.

Permanently out, and the docs must say so: session lock and PAM, being the notification server and
invoking toast actions, DBus tray menus rendered in QML, alpha-thresholded compositor blur, Hyprland
special workspaces and `dispatch`, exclusive keyboard focus.

## 6. Naming deviations in the plan

| Plan (2026-10-29) | Quickshell |
|---|---|
| `Shell` | `ShellRoot`, `Scope` |
| `Panel` / `Overlay` with `edge`, `thickness`, `length`, `exclusive` | `PanelWindow` with `anchors.*`, `implicitWidth/Height`, `exclusiveZone`, `exclusionMode` |
| `screens: [...]` | `Variants { model: Quickshell.screens }` |
| `Window.window.surface` | `QsWindow.window` |
| `Persist` | `PersistentProperties { reloadableId }` |
| `Hotkey { keys }` | `GlobalShortcut { name, appid, description }` |
| `Process { program, stream, line(), json, exited(code) }` | `Process { command, stdout: StdioCollector, exited(exitCode, exitStatus) }` |
| `Shell.env`, `Shell.start` | `Quickshell.env()`, `Quickshell.execDetached()` |
| `Desktop.screens` | `Quickshell.screens` |
| `PopupWindow.anchorItem`, `anchorRect` | `anchor.item`, `anchor.rect` |
| `Loader { active }` | `LazyLoader { loading }` |
| `Apps` | `DesktopEntries`, `DesktopEntry.execute()` |
| `S.Windows` | `ToplevelManager.toplevels` |
| `focus: Surface.OnDemand` | `WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand` |
| `input: PassThrough` | `mask: Region {}` |

`IpcHandler` is the one type the plan already has in Quickshell's exact form.

## 7. Next steps

1. The owner decides open questions 12-15 in section 11 of the extensibility proposal.
2. Run spikes S7 (input region by cursor polling) and S8 (loading a user-built QML plugin) before
   migration step 1 is sized.
3. Fold the vocabulary change into migration step 1, since it is the breaking commit.
