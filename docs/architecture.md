# Architecture

How waku is put together and the rules that keep it that way. Rules for contributors and agents
are in `AGENTS.md`; build and run commands are in `docs/building.md`. The decisions behind the
layout are in `docs/plans/2026-10-29-waku-architecture-proposal.md`.

## Units and layering

Dependencies point one way: `app` -> `quickshell` -> `features` -> (`host`, `control`, `ui`) ->
`kernel`. Every unit
under `src/` is its own static library or contributes to a QML module. The include root is `src/`.
The `structure` step of `tools\check.ps1` enforces the direction on includes and QML use (rules
S1-S8; the table at the top of that step is the exact text). Every own target, the executables and
`waku_tests` link `waku_options` (`cmake/waku_options.cmake`: `/W4 /WX /permissive- /guard:cf
/sdl ...`).

| Target | Folder | Sources | Links PUBLIC | Links PRIVATE |
|---|---|---|---|---|
| `waku_kernel` | `src/kernel/` | shared helpers (below) | `Qt6::Core`, `Qt6::Gui` | `waku_options`, Win32 libs |
| `waku_control` | `src/control/` | the control pipe between a running bar and `wakuc` (no Qt Quick) | `waku_kernel`, `Qt6::Core`, `Qt6::Network` | `waku_options` |
| `waku_host_base` | `src/host/` | `screen_match`, `popup_placement`, `panel_geometry`, `app_bar.h`, `thumbnail.h` and `win32/` (app bar, DWM backdrop, DWM thumbnails) | `waku_kernel` | `waku_options`, `dwmapi shell32 user32` |
| `waku_host` | `src/host/` | the shell runtime: QML module `waku` (`qml/`: `ShellHost`, the window types, `win32/shell_host_win32.cpp`) | `waku_host_base`, `waku_control`, Qt Quick | `waku_options`, `Qt6::QmlPrivate` (one use: a compiled shell's root type, `shell_host.cpp`) |
| `waku_quickshell_modules` (interface) | `src/quickshell/` | everything below and the static plugins; the one target of the unit that `waku` and `waku_tests` link | `waku_quickshell`, `waku_quickshell_services` | |
| `waku_quickshell` | `src/quickshell/core/` | QML module `Quickshell`: the `waku` shell types and three feature types registered again under Quickshell's names (`QML_FOREIGN`) and two QML wrappers | `waku_host`, Qt Quick | `waku_options`, `waku_services` |
| `waku_quickshell_{wayland,widgets,hyprland,upower,pipewire,systemtray,bluetooth,networking}` | `src/quickshell/{wayland,widgets,hyprland,bluetooth,networking}/`, `src/quickshell/services/{upower,pipewire,systemtray}/` | one QML module each (`Quickshell.Wayland`, `.Widgets`, `.Hyprland`, `.Services.UPower`, `.Services.Pipewire`, `.Services.SystemTray`, `.Bluetooth`, `.Networking`), made by `waku_quickshell_qml_module()` (`src/quickshell/CMakeLists.txt`) and linked into `waku_quickshell` | `waku_host`, Qt Quick | `waku_options`, `waku_services` |
| `waku_quickshell_services` (interface), `waku_quickshell_{io,mpris,notifications}_module` | `src/quickshell/{io,services/mpris,services/notifications}/` | the QML modules `Quickshell.Io`, `Quickshell.Services.Mpris` and `Quickshell.Services.Notifications`, made by `waku_quickshell_service_module()` (`src/quickshell/CMakeLists.txt`): feature types registered again under Quickshell's names from `qml/quickshell_types_*.h`; the interface target carries the three modules and their plugins | the three module targets (each `waku_kernel`, Qt Quick) | `waku_options`, `waku_services` |
| `waku_test_module` (shared plugin) | `tests/plugin/` | the QML module `WakuTestModule`, built only for `waku_tests`: it stands for a plugin a user drops into `<config home>/modules` | Qt Quick | `waku_options` |
| `waku_<f>` | `src/features/<f>/` | one feature (`waku_feature()`) | `waku_kernel`, the `DEPENDS` features | `waku_options`, the feature's system libraries |
| `waku_services`, `waku_widgets` | `src/modules/qml_modules.cmake` | QML modules `waku.services` and `waku.widgets`, assembled from contributions (below) | the feature libraries, Qt Quick | `waku_options` |
| `waku` (exe), `wakuc` (exe) | `src/app/waku/`, `src/app/wakuc/` | `main.cpp` of each; `SafeShell.qml` and `DefaultShell.qml` join `waku.widgets` | | `waku_options`, the modules / `waku_control` |
| `WakuTrayHook[_arm64]` (DLL) | `src/features/tray/hook/` | `trayhook.cpp` | | static CRT, `/W4 /WX` set by hand, built next to `waku.exe` |
| `waku_tests` (exe) | `tests/` | `tests/main.cpp`, `tests/integration/` and every `TESTS` contribution (`src/**/tests/`) | | `waku_options`, `GTest`, `Qt6::Test` |

`ui` has no C++: its QML files (`Pill`, `Slider`, `DeviceMenu`, `DeviceRow`) join `waku.widgets`.

The one relaxation of the warning level is `waku_qt_generated_code()` in `cmake/waku_options.cmake`:
`/wd4702` on the files qmlcachegen generates for the three QML modules.

### Allowed dependencies

A feature declares its edges in `waku_feature(... DEPENDS ...)` in its `CMakeLists.txt`. It may
include the root headers (interfaces and value types) and use the QML types of its `DEPENDS`; never
their `win32/` or `qml/` C++ headers, and `testing/` only from tests. The graph is acyclic.

| Unit | May depend on |
|---|---|
| `kernel` | Qt Core, Qt Gui; the Windows SDK only in `kernel/win32/` |
| `control` | `kernel`, Qt Core, Qt Network |
| `host` | `kernel`, `control`, Qt Quick |
| `ui` (QML only) | the `host` QML types (`Theme`, `Tooltip`, `PopupWindow`, `QsWindow`) |
| every feature | `kernel`; its QML also uses the `host` QML types and `ui` |
| `quickshell` | `kernel`, `control`, `host/qml/`, the root and `qml/` headers and the QML types of any feature, Qt Quick. Only `app` and tests include it. Layout and the type table: `src/quickshell/README.md` |
| `audio`, `bluetooth`, `network`, `github` | `quickshell_io` (the "open settings" and "open URL" clicks use the `Shell` service) |
| `control_center` | `audio`, `media`, `notifications`, `brightness` |
| `apps`, `brightness`, `clock`, `color_scheme`, `desktop_state`, `media`, `notifications`, `quickshell_io`, `startup`, `system_stats`, `taskbar`, `tray`, `wallpapers`, `workspaces` | no other feature |
| `app`, `tests/` | everything |

`startup` registers the logon Scheduled Task of `wakuc startup` through the documented Task Scheduler COM API (`ITaskService`), so it adds no row to the undocumented API allowlist.

A feature may also include the root headers of `control/` (the pipe protocol in `control.h`):
they are a stable shared interface, not a feature edge. `host` includes `control/` too. Neither
`control` nor `host` knows a feature's command: a feature exports its verb as a function (the
`wallpaper` verb is `features/wallpapers/wallpaper_command.h`), and `src/app/waku/main.cpp` adds it
to the bar's control handler (`host::ControlCommands` in `src/host/qml/control_handler.h`).

### Feature layout

Folders a feature does not need are absent. `waku_feature()` rejects a file in the wrong folder at
configure time.

| Path | Holds | May include |
|---|---|---|
| `CMakeLists.txt` | the one `waku_feature(...)` call: sources, `DEPENDS`, system libraries, QML contributions, tests | |
| `design.md` | the design note of the capability | |
| feature root `*.h`, `*.cpp` | the domain: backend interfaces, value types, UI-free logic, factory declarations | Qt Core and Gui; no Windows headers, no QML |
| `win32/` | the Windows implementation of the root interfaces (Win32, COM, WinRT); `_win` suffix where a root header has the same name | Windows SDK and C++/WinRT |
| `qml/` | QML-facing C++ (singletons, models, image providers) and the feature's `.qml` files | `QtQml`, `QtQuick`, `QML_*` macros |
| `testing/` | fakes of the root interfaces, header-only | used by tests only |
| `tests/` | gtest sources; suite names start with the feature name | anything of the feature, `kernel`, `testing/` of itself and its `DEPENDS` |

### CMake

- `cmake/waku_feature.cmake`: `waku_feature(NAME <f> DEPENDS ... SOURCES ... WIN32_SOURCES ...
  QML_SOURCES ... QML_FILES ... TESTS ... SYSTEM_LIBS ...)` builds `waku_<f>` (static, or
  interface when the feature is QML only), links `waku_kernel` and the `DEPENDS` libraries, checks
  the folder layout and that each `DEPENDS` feature is declared earlier, and appends the feature's
  QML C++, `.qml` files and tests to global lists. `waku_contribute()` does the same append for
  kernel, host, ui, quickshell (its tests) and app. The comment at the top of the file holds the decisions. `WINRT_PCH
  <headers...>` precompiles C++/WinRT headers (after `unknwn.h`) for the `WIN32_SOURCES`; audio,
  media, network and notifications use it.
- `src/CMakeLists.txt` adds `kernel`, `control`, `host`, `ui`, `features`, `quickshell` and `app`, then includes
  `src/modules/qml_modules.cmake`. `src/features/CMakeLists.txt` has one `add_subdirectory` line
  per feature, in dependency order. Deleting a feature is deleting its folder and that line.
- `tests/CMakeLists.txt` defines `waku_tests` from `tests/main.cpp`, `tests/integration/` and the
  `TESTS` list. One binary keeps one `windeployqt` step and one link; select a feature with
  `ctest -R Tray` or `--gtest_filter=Tray*`.
- System libraries sit in the `SYSTEM_LIBS` of the feature that calls them.

## The kernel

`src/kernel/` holds helpers with no domain knowledge that two or more units use and that change for
reasons outside any feature. Admission criteria are in `AGENTS.md` (a pull request that adds a
kernel file names its two users). Qt Core and Gui only; the Windows SDK only under `kernel/win32/`.
Namespace `waku::kernel`, include `"kernel/<qt|win32>/<file>.h"`.

| File | What | Users |
|---|---|---|
| `win32/windows_build.*` | `windowsBuild()`, cached `RtlGetVersion` (allowlist row below) | host backdrop, `src/features/system_stats/win32/pdh.cpp`, `src/features/control_center/win32/power_win.cpp` |
| `win32/window_focus.*` | `forceForegroundFocus(hwnd)`: `SetForegroundWindow` and `SetFocus` with the documented `AttachThreadInput` workaround, so a window shown from a hotkey or from `wakuc` gets the keyboard focus | host (`NativeHooks::forceForeground` for windows with `keyboardFocus` not None), wallpapers (`Wallpapers::focusWindow`, the gallery), taskbar (`WindowControl::activate`) |
| `win32/winrt_apartment.*` | `ensureWinrtApartment()` | bluetooth, brightness, network (`connectivity_win`, `wlan_win`) |
| `win32/com_scope.*` | `ComScope(ComApartment[, disableOle1Dde])`: `CoInitializeEx` for one scope, balanced on destruction (S_OK and S_FALSE counted, `RPC_E_CHANGED_MODE` keeps the thread's apartment) | kernel (`shell_icons`, `shell_launch`), apps, taskbar, wallpapers, notifications (`dnd_win`), control_center (touch keyboard), audio |
| `win32/environment.*` | `expandEnvironment(text)`: `%VARIABLE%` expansion (`ExpandEnvironmentStringsW`) | apps (Start Menu shortcuts), taskbar (pinned shortcuts) |
| `win32/winrt_strings.h` | `toQString(hstring)` (the whole string, by size) and `toHString(QString)` | media, notifications, bluetooth, brightness, wallpapers (lock screen), kernel (`task_thread`) |
| `win32/task_thread.*` | `TaskThread`: one MTA `std::thread` that runs posted tasks in order (`post`, a coalesced `postDeferred`, `stopRequested`, a `Poster` for delegates that outlive it, `stop` joins), with set-up and tear-down on the worker and per-task exception logging | media (GSMTC session thread), notifications (listener worker), wallpapers (lock screen worker) |
| `win32/shell_launch.*` | `shellOpen`, `shellRunElevated`, `launchHiddenShell`, `startDetachedHidden`, `hideConsole` | Shell, ControlCenter, toast activation, komorebi, `ShellProcess`, `quickshell_io` (`Process`, `execDetached`) |
| `win32/shell_icons.*` | `IconBitmap`, `shellItemIcon` (`IShellItemImageFactory`, documented), `iconBitmapFromHicon` | taskbar (window and pinned-app icons), apps (`image://appicon`) |
| `win32/app_activation.*` | AUMID lookups and `activateAppByAumid` (documented Win32/COM only) | media source names, toast activation, apps (`win32/launch_win.cpp`) |
| `qt/live_desktop.*` | `liveDesktop([capability])`, `setCapabilityDisabled`, `capabilityDisabled` | the services' real constructors, `kernel::Hotkeys`, `src/app/waku/main.cpp` (`--no-tray`, `--no-hotkeys`) |
| `qt/hotkeys.*`, `win32/hotkeys_win.cpp` | `Hotkeys` (the process-wide global hotkey table: `add(combo, callback)` returns a handle, `remove(handle)`; two users on one combination share one registration and both fire), `parseHotkey("alt+e")`, the `HotkeyBackend` interface and its `RegisterHotKey` implementation with a native event filter for `WM_HOTKEY`; the fake backend is `testing/hotkeys_fixtures.h` | host (`GlobalShortcut`, so also the Quickshell `GlobalShortcut` types), wallpapers (the gallery hotkey of `Wallpapers`) |
| `qt/polled_source.*` | `PolledSource`, the polled-sample base | `Memory`, `Cpu`, `Battery`, `Network` |
| `qml/stat_source.h` | `StatSource`, the QML face of `PolledSource` (`QML_ANONYMOUS`, `QQmlParserStatus`); compiled into `waku.services`, not into `waku_kernel` | `Memory`, `Cpu`, `Battery`, `Network` |
| `qt/linger.*` | `linger(held, ms)` and `LingeringHub<T>`: a process-wide hub that outlives its last holder by a grace period, so a reload does not stop and restart it | media (`acquireMediaService`), notifications (`acquireNotificationService`), system_stats (the data source factories) |
| `qt/gui_post.h` | `postToGui(QPointer, fn)`: runs `fn` on the GUI thread through qApp if the guard is still alive there | audio, color_scheme, github, network (radio, VPN), control_center, wallpapers (transition decode), taskbar (icon loads) |
| `qt/file_write.*` | `writeFileAtomic(path, bytes)`: `QSaveFile` into a created folder, the error and its text as the result | taskbar (pins), github (token), tray (state), apps (frecency), color_scheme (image stats), host (taskbar marker), quickshell_io (`FileView` atomic writes) |
| `qt/app_data.*` | `appDataDir()`, `migrateLegacyAppData()` | taskbar pins, GitHub token, tray state, host thumbnails (`src/host/qml/thumbnails.cpp`), apps frecency (`apps_service.cpp`), `color_scheme` image stats, `quickshell_io` state, cache and data folders (`quickshell_singleton.cpp`) |

Rules the kernel owns:

- **One data folder**: every file waku keeps lives in `%LOCALAPPDATA%\waku` (`appDataDir()`,
  created on demand). Nothing writes to `%LOCALAPPDATA%\YASB`. The first `appDataDir()` call of a
  process copies `taskbar_pinned.json`, `github_token` and `systray_state_*.json` from `YASB` when
  `YASB` exists; it never moves or overwrites, logs one line when it copied something and leaves a
  marker file `migrated_from_yasb`, so the copy happens once (a file the user deletes from `waku`
  does not come back). Thumbnails are a cache and are rebuilt, not copied.
- **Live desktop**: a service that touches the real machine in its real constructor asks
  `kernel::liveDesktop()`; it is false on the offscreen platform (tests, screenshots). A
  command-line flag switches one capability off by name (`setCapabilityDisabled("tray", true)`);
  ask `liveDesktop("tray")`. No service tests `platformName()` itself.

## QML modules and built-in widgets

There are 14 QML modules. Their URIs are the user-facing API and do not change. Three are the
engine's own: `waku` (2.0: the Quickshell vocabulary replaced `Config`, `Bar` and `BarLayout`; the
mapping is in `docs/building.md`), `waku.services` and `waku.widgets` (1.0). Eleven are
Quickshell compatibility modules: `Quickshell`, `Quickshell.Wayland`, `Quickshell.Widgets`,
`Quickshell.Hyprland`, `Quickshell.Services.UPower`, `Quickshell.Services.Pipewire`,
`Quickshell.Services.SystemTray` and `Quickshell.Bluetooth` (0.1), and `Quickshell.Io`,
`Quickshell.Services.Mpris` and `Quickshell.Services.Notifications` (1.0). The table lists them.
`WakuTestModule` (tests only) is not one of the 14.

| URI | Built from | Holds |
|---|---|---|
| `waku` | `src/host/qml/` | `ShellRoot`, `Scope`, `PanelWindow`, `FloatingWindow`, `QsWindow` (attached), `Variants`, `LazyLoader`, `GlobalShortcut`, `Region` (one notify signal per property: `xChanged`, `yChanged`, `widthChanged`, `heightChanged`, `shapeChanged`, `itemChanged`, `intersectionChanged`; `treeChanged` is internal), `Intersection`, `RegionShape`, `ScreenList`, `ShellScreen`, `WlrLayershell` (attached), the enums `ExclusionMode`, `WlrLayer`, `WlrKeyboardFocus`, `Edges`, `DockEdge`, `Backdrop`; the presets `Panel` and `PanelLayout`; `Theme`, `ThemeSpec`, `PopupWindow`, `Tooltip`, `PopupAnchor` (the `anchor` of a `PopupWindow`), `PopupPlacement` (`edgeForDock(dockEdge)` gives the side a popup opens on; `anchorRect(item, panel, vertical)` extends an item across its panel's thickness). Internal: `WindowChrome`, `FallbackShell` |
| `Quickshell` | `src/quickshell/core/` | `ShellRoot`, `Scope`, `PanelWindow`, `FloatingWindow`, `PopupWindow`, `Variants`, `LazyLoader`, `GlobalShortcut`, `QsWindow`, `ShellScreen`, `ExclusionMode`, `Edges`, `Region`, `Intersection`, `RegionShape`: the same types as `waku`, except `PopupWindow` and `Variants`, which are QML wrappers of the `waku` ones and so distinct QML types with the same behaviour. The `Quickshell` singleton (`Quickshell.qml`): `screens` from `ScreenList`, every other member forwarded to `waku.services.QuickshellSingleton`; `src/app/waku/quickshell_glue.h` gives it the config home and joins `reload()` to the host |
| `Quickshell` (also) | `src/quickshell/core/qml/DesktopEntries.qml`, `src/quickshell/core/qml/quickshell_types_core.h` | `DesktopEntries` (forwards to `waku.services.AppsService`); `DesktopEntry`, `DesktopAction` and `ElapsedTimer` are the feature types registered again under these names (QML_FOREIGN). The module depends on `waku.services` but does not re-export it: `import Quickshell` shows Quickshell's names only, so a shell's own `Wallpapers.qml` or `Workspaces.qml` is never shadowed. `Quickshell.Io` (`src/quickshell/io/`), `Quickshell.Services.Mpris` (`src/quickshell/services/mpris/`; `Mpris` is the same singleton as `waku.services.Mpris`) and `Quickshell.Services.Notifications` (`src/quickshell/services/notifications/`) follow the same rule |
| `Quickshell` (items) | `src/host/qml/` (`scope.h`, `transform_watcher`), registered in `waku` and again in `Quickshell` | `Singleton` (a `Scope`: the root of a `pragma Singleton` file; children go to `data`), `WindowThumbnail` (`hwnd`, `active`, `fit`, `sourceWidth`, `sourceHeight`, `available`: a live DWM thumbnail of another window, `src/host/qml/window_thumbnail.*`; backend `thumbnail.h`, documented DWM APIs only), `TransformWatcher` (`a`, `b`, `commonParent`, read-only `transform` with `transformChanged()`: fires when x, y, size, scale, rotation or parent of an item on the path from `a` to `b` changes; listens to the items below the nearest common ancestor and rebuilds the path on a reparent) |
| `Quickshell.Wayland` | `src/quickshell/wayland/` | `WlrLayershell` (attached to a `PanelWindow`), `WlrLayer`, `WlrKeyboardFocus` |
| `Quickshell` (core types) | `src/host/qml/` (`script_model`, `system_clock`, `persistent_properties`, `ipc_handler`), registered in `waku` and again in `Quickshell` | `ScriptModel` (`values`, `objectProp`: a list model that inserts, removes and moves rows by key, so a view keeps its delegates), `SystemClock` (`enabled`, `precision` Hours/Minutes/Seconds, `date`, `hours`, `minutes`, `seconds`; one timer aimed at the precision boundary), `PersistentProperties` (`reloadableId`, the properties the QML declares; a store per engine that each object updates when a declared property changes and that the next object of the same id reads when it completes; `loaded()`, `reloaded()`), `IpcHandler` (`target`, `enabled`; the functions declared in QML answer `wakuc call`, see `docs/building.md`) |
| `Quickshell.Widgets` | `src/quickshell/widgets/` | `IconImage`, `ClippingRectangle` (a MultiEffect mask, so `QtQuick.Effects` must be deployed), `ClippingWrapperRectangle`, `WrapperRectangle`, `WrapperItem`, `WrapperMouseArea`; `WrapperManager` is their shared layout and not exported. Plain QML. Margins reset by `undefined` are not supported |
| `Quickshell.Hyprland` | `src/quickshell/hyprland/` | Compile compatibility for shells, not Hyprland: the `Hyprland` singleton has empty `workspaces`, `monitors` and `toplevels` models, null `focusedWorkspace`, `focusedMonitor` and `activeToplevel`, no-op `dispatch` and `refresh*`, and a `rawEvent` that never fires. `GlobalShortcut` is the `Quickshell` one. `HyprlandFocusGrab` (`windows`, `active`, `cleared()`) ends, and sets `active` false, when none of its windows is the active window (armed once one was) or all are hidden |
| `Quickshell.Services.UPower` | `src/quickshell/services/upower/` | `UPower` (`displayDevice`, `devices`, `onBattery`) and `PowerProfiles` (`profile`, `hasPerformanceProfile`) with the `UPowerDevice` type and the `UPowerDeviceState`, `UPowerDeviceType`, `PowerProfile` and `PerformanceDegradationReason` enums. `percentage` is a fraction 0..1 (the Quickshell docs define it as `energy / energyCapacity`); energy, rate and health are 0. Profiles map the Windows power modes by name. `UPowerBase` is C++ because QML cannot declare `onBattery` |
| `Quickshell.Services.Pipewire` | `src/quickshell/services/pipewire/` | `Pipewire` (`defaultAudioSink`, `defaultAudioSource`, `nodes`, `ready`, `preferredDefaultAudioSink/Source`, empty `links` and `linkGroups`), `PwNode` (`audio.volume` 0..1 and `audio.muted`, writable through the audio service; `properties`, `name`, `description`, `isSink`, `isStream`), `PwNodeAudio`, `PwObjectTracker`, `PwNodeLinkTracker` (no-ops), `PwLink`, `PwLinkGroup`. The nodes are the default output and input and one per app audio session. `PwNode.id` is missing: QML cannot declare it |
| `Quickshell.Services.SystemTray` | `src/quickshell/services/systemtray/` | `SystemTray.items` (a model with `values`) of `SystemTrayItem` (C++, `id` is the executable name; `title`, `tooltipTitle`, `icon`, `status` Active, `hasMenu` false, `menu` null; `activate()` left click, `secondaryActivate()` middle click, `display()` right click, `scroll()` nothing) and `Status` |
| `Quickshell.Bluetooth` | `src/quickshell/bluetooth/` | `Bluetooth` (`defaultAdapter`, `adapters`, `devices`), `BluetoothAdapter` (`enabled` switches the radio; `discovering` starts one scan and turns false when it ends; `devices` include the nearby unpaired ones after a scan), `BluetoothDevice` (`connected` requests a connect or disconnect and reads back what Windows reports; `state` is `Connecting` or `Disconnecting` while the request runs; a failure sets the service `error`; `battery`; `pair`, `cancelPair` and `forget` call the service; `pairing`) and the state enums |
| `waku.widgets` | every feature's `qml/*.qml`, `src/ui/*.qml`, `src/app/waku/SafeShell.qml`, `src/app/waku/DefaultShell.qml` | the built-in widgets and the shared look |
| `waku.services` | every feature's `qml/*.{h,cpp}`, `src/kernel/qml/` | the C++ data sources and actions |

- Some engine types exist for ported shells, not for the built-in widgets: `WindowThumbnail`,
  `TransformWatcher`, `PersistentProperties`, and `ScriptModel` (used only by the `Quickshell.*`
  modules). `Thumbnails` serves both (the wallpaper gallery uses it). `examples/gallery` shows
  `Thumbnails` and `WindowThumbnail`; `tests/integration/test_gallery_example.cpp` loads it.
- Module `waku` holds two kinds of type. The engine: `ShellRoot` to `Region` (windows, screens,
  `Variants`, `LazyLoader`, `GlobalShortcut`, regions, `WlrLayershell` and the enums), plus
  `PopupAnchor` and `PopupPlacement`, which a ported shell uses too. The built-in look: `Theme`,
  `ThemeSpec` (`src/host/qml/theme.h`), `Panel`, `PanelLayout`, `Tooltip`, `PopupWindow`'s default
  chrome and `WindowChrome`. The built-in widgets use the look; a ported shell carries its own and
  uses none of it. The look sits in `host` only because `ShellHost` loads `theme.qml` and applies it
  on every reload. It stays there for now. When `src/ui/` gains C++, a later step may move it into
  a `waku.ui` module; the styling proposal (`docs/plans/2026-10-29-waku-styling-proposal.md`,
  sections 1.1 R3 and 4.1) discusses where the tokens should live.
- `surfaceFormat.opaque` on `PanelWindow` and `FloatingWindow` is accepted and ignored, so Quickshell
  code that sets it loads. waku always draws into a swap chain with an alpha channel.
- `src/modules/qml_modules.cmake` assembles the last two from the contributions. It is included
  from `src/CMakeLists.txt`, not added as a subdirectory, because qmlcachegen names its outputs after
  the `.qml` path relative to the declaring directory and a `../` path breaks the build on Windows.
- Every `.qml` file gets `QT_RESOURCE_ALIAS <basename>`, so resource paths stay
  `qrc:/qt/qml/waku/widgets/<Name>.qml`. Basenames are therefore unique per module (rule S7), and
  the generated type registration includes QML-facing headers by name, so those are unique too.
- A type's QML name is its class name or `QML_NAMED_ELEMENT`; its C++ namespace (`waku::tray`)
  is not visible to QML.
- Widgets and services share some names (`Cpu`, `Memory`, `Battery`, `Media`, `Workspaces`). A widget
  imports the services as `import waku.services as S`, and so does a user config that needs them.
  This clash is why there is one module per kind and not one per feature.
- A built-in `.qml` file whose delegates or inline components use ids of the enclosing file starts
  with `pragma ComponentBehavior: Bound`, so qmllint and qmlcachegen know those ids. A delegate in
  such a file gets no context properties from its view: it takes `model`, `modelData` or `index`
  through `required property` declarations only.

### How built-in widgets ship, and how to override one

The built-in widgets stay compiled into `waku.exe` (qmlcachegen, resources, static plugins). The
source of one is `src/features/<f>/qml/<Name>.qml`. `deploy` also copies those files with `qmldir`
and `.qmltypes` to `dist/waku/modules/` as a read-only reference (`docs/building.md`).

A user changes a built-in widget by copying its file into the config home's `widgets/` folder,
which `shell.qml` imports as a directory (`import "widgets"`), and editing the copy. Give the copy a
new name and use that name in `shell.qml`. A copy that keeps the built-in's name relies on QML's
import order and is not covered by tests. `qmllint -I <dist>/waku/modules` lints the copy. The
config format is described in `docs/plans/2026-10-28-qt-quick-bar-config-design.md`.

### User-built QML plugins

`src/host/plugin_guard.*` (Qt Core only) decides which import folders the engine may see; the
composition root (`src/app/waku/main.cpp`) adds the survivors with `QQmlEngine::addImportPath`.
The folders are `<config home>/modules` and each `--import-path`; `--safe` adds none. For every
`plugin` line of a `qmldir` under a folder the guard resolves `Name.dll` (`Named.dll` in a debug
build), checks the PE machine against the process, then reads the file's embedded
`QtPluginMetaData` with `QPluginLoader::metaData()` (no load) and compares `version` (major and
minor) and `debug` with this build. A failing plugin refuses its whole folder, because the
throwaway engine that `ShellHost::validate` creates copies the main engine's import paths and the
granularity of a path is a folder. `ShellHost::reload` also runs `configPluginFailures` over every
other `qmldir` under the config home (import roots already checked are skipped; symbolic links,
`.git`, `node_modules` and depth beyond 8 are not walked) and refuses the reload with the file and
reason when one fails. Tests: `tests/integration/test_plugin_import.cpp` and the plugin
`tests/plugin/`.

### The shell runtime

`ShellHost` (`src/host/qml/shell_host.*`) loads `<config home>\shell.qml`. Before it replaces the
running shell it compiles the new file in a throwaway engine and checks that the root is a
`ShellRoot`; nothing is created there, so no window, widget or service starts. Then the old shell
goes (its app bars first), the theme is applied and the new shell is created in the main engine.

Every window type derives from `QsWindow` (`src/host/qml/qs_window.*`; the subclasses and the
`WlrLayershell` attached type each have their own file beside it: `panel_window.*`,
`floating_window.*`, `wlr_layershell.*`): a declaration that owns a backing
`QQuickWindow` and puts its children into that window's content item. On completion a window
registers with the `ShellHost` of its engine. Windows created while the shell builds are shown when
it is installed; a window created later (`Variants`, `LazyLoader`) is shown at once. A window whose
`content` component fails is left out and its error recorded; the fallback shell appears only when
no window of the shell could be built. `PanelWindow` geometry is the pure `panelGeometry` in
`panel_geometry.*`.

A panel is docked to the edge it is anchored to when it is anchored to one edge, not to the
opposite one, and is stretched (both perpendicular edges) or centred (neither) across it
(`panelAppBarEdge`). A corner window, a band across the screen and a full-screen window are not
docked. The derived `dockEdge` (a `DockEdge` value) and `dockVertical` of a `QsWindow` expose that
edge to the content; they carry the `dock` prefix so a Quickshell shell that declares its own
`edge` or `vertical` on a window loads. `PopupWindow`, `Tooltip`, `PanelLayout` and `Pill` read
them: popups open away from the edge (below a top panel, above a bottom one, right of a left one,
left of a right one), groups run top to bottom on a left or right panel, and a `Pill` stacks its
icon above its text. `Strip` (`src/ui/Strip.qml`) is the positioner of the built-in widgets that
hold several items (`Workspaces`, `Taskbar`, `Tray`).

A docked panel is an app bar on its edge (`AppBarEdge` Left 0, Top 1, Right 2, Bottom 3, the ABE
values) and reserves work area only with `--register-app-bar` and a non-zero `exclusiveZone` or
thickness (`panelReservedZone`). The window is registered (`ABM_NEW`) only when it reserves space.
Rects are physical pixels: the origin of `QScreen::geometry()` (Qt keeps it in native coordinates)
and its size times the device pixel ratio (`computeAppBarRect`). `negotiateAppBarRect` calls
`ABM_QUERYPOS`, re-applies the thickness to the adjusted rect, then `ABM_SETPOS`. `IAppBar::create`
returns the inset the shell granted (another app bar or the Windows taskbar on the same edge); the
host stores it on the `PanelWindow` (`setAppBarInset`, logical pixels) and `applyGeometry` keeps
the window that far in on every pass. When the placement, the reserved zone, the layer or the
focus mode changes, `ShellHost::syncAppBar` compares the new request with the last one and calls
`IAppBar::reposition` (the same `ABM_QUERYPOS`/`ABM_SETPOS` without a new `ABM_NEW`), or removes the
bar when the panel is no longer docked.

`ShellScreen` mirrors `QScreen`. `width` and `height` are logical (device-independent) pixels, the
units of window sizes and margins: `QScreen::size()` is the physical size divided by
`devicePixelRatio`, so a 3840x2560 panel at ratio 1.5 reports 2560x1707 (measured with Qt 6.10.2 on
Windows). `x` and `y` are the origin of `QScreen::geometry()`: native pixels in Qt 6, the space in
which window positions are set (`geometryOn` adds logical offsets to that origin). `devicePixelRatio`
turns a logical size into physical pixels (`width * devicePixelRatio`). `test_shell_screen.cpp`
checks this on offscreen screens at ratio 1.5 and 2.0 with a non-zero origin.

### Input masks

A window's `mask` (`Region`, `src/host/qml/region.*`) restricts where it takes input; the rest
clicks through to the window below. Windows has no region-based hit test for a Qt window, so the
host polls (`MaskPoller`, `src/host/mask_poller.*`). The measured facts behind it: `WS_EX_TRANSPARENT`
alone does not click through, but `WS_EX_LAYERED | WS_EX_TRANSPARENT` does, and
`Qt::WindowTransparentForInput` set at run time through `QWindow::setFlag` produces exactly that
(2 to 4 ms per call). The DWM backdrop is not affected by the flip.

- One `MaskPollerHub` per `ShellHost` owns the only timer (a `Qt::PreciseTimer`), whatever the
  number of masked windows; each masked, visible window has a `MaskPoller` registered with it. A
  tick reads the cursor once (`GetCursorPos`, physical pixels; documented, so no allowlist row) and
  hands it to every poller, which converts it to its window's logical coordinates
  (`cursorInWindow`: screen origin, device pixel ratio, window position) and tests it against the
  mask grown by 6 px (`regionContainsPadded`: the point and eight points 6 px away). The flag is
  set only when the answer changes.
- The rate is 8 ms (120 Hz) while the cursor moved within the last 300 ms, 33 ms (30 Hz) otherwise,
  as long as the cursor is near an edge of a padded mask. Farther away the next tick waits as long
  as a cursor at 20,000 physical px/s needs to reach the nearest edge (`regionEdgeDistance`), up
  to 250 ms (4 Hz): no answer can change sooner, so the flip latency stays that of the two rates
  for any cursor up to that speed. A cursor that jumps (touch, a program moving it) can meet a
  slow tick.
- A tick where the cursor did not move skips a window unless it is dirty: its region changed
  (`Region::treeChanged`), the window asked for a frame (`QQuickWindow::afterAnimating`, on the GUI
  thread, which every moving item causes) or the window moved or resized. A dirty window also
  brings the next tick forward to the rate above. Every window is tested at least once a second
  anyway, against the last mask. The mask (a region tree built from QML) is built again only for a
  dirty or moved window; a moving cursor and the once-a-second check test the last one. So a resting
  cursor over an idle bar costs a cursor read at up to 30 Hz and nothing else. The timer stops
  when no window is masked and visible.
- The poll stops when the window hides or the mask goes, and the flag is cleared then. A cursor
  outside the window is simply outside the region.
- Region math is a pure function over `RegionNode` (`src/host/region_math.*`): the shape (a
  rectangle or an ellipse, or the bounds of `item` mapped to the window), then each child folded in
  order by its `Intersection` (Combine = union, Intersect, Xor, Subtract). The root's own
  `Intersection` combines it with the window rectangle (`regionContainsInWindow`): Combine uses the
  root alone, the others give `window <op> root`, so a root `Xor` (Caelestia) takes input outside
  the inner area plus inside the open panels. An empty `Region {}` contains nothing, so the window
  is fully click-through; no `mask` means the whole window takes input.
- The cursor comes from `ShellHost::NativeHooks::cursorPosition`. `win32()` sets it only when
  `kernel::liveDesktop()`; tests and the offscreen platform have none (or a fake), so masks are
  inert there and no test reads the real cursor or flips a real window.
- Known race: a click in the gap between the cursor reaching a region and the next poll (up to
  8 ms while the cursor moves; up to 33 ms for the first tick after a rest) goes to the window
  below. The 6 px pad makes the flip happen before a normal approach
  arrives. A click that starts on a click-through spot and drags into a region is not seen by the
  window.
Overlays (a launcher, an OSD, a power menu): any window can be shown on demand. A window with
`visible: false` makes no native window and no swap chain until it is first shown (a `QQuickWindow`
creates both at `show()`); a hidden overlay's content can be built lazily with a `LazyLoader`.
`show()`, `hide()` and `toggle()` set `visible`; `shown()` and `hidden()` fire when the native window
appears and leaves; `hideDelay` (ms) keeps the window up after `visible` turns false so the content
can fade out, and showing again cancels the hide. Layer and keyboard focus reach Windows through
`ShellHost::NativeHooks`, so tests record them: `Top` and `Overlay` call `raiseTopmost`
(`SetWindowPos(HWND_TOPMOST)`, no activation) each time the window is shown, `Bottom` stays on the
Qt hint `Qt::WindowStaysOnBottomHint`, `Background` is still `Bottom`. `keyboardFocus: None` sets
`WS_EX_NOACTIVATE` (`setNoActivate`) and `Qt::WindowDoesNotAcceptFocus`. Any other value is
activated when shown at run time (not when the shell is first installed or re-shown after a
reload): `requestActivate()` plus `forceForeground`, which is `kernel::forceForegroundFocus`
(`AttachThreadInput`, documented). Windows only grants the foreground to the process that got the
last input, so `wakuc surface show|toggle` reads the bar's pid from `status` and calls
`AllowSetForegroundWindow(pid)` first. `dismissOnBlur` hides a focusable window that was active and
lost the activation; `dismissOnEscape` hides it when Escape reaches the window and no item took it.
`name` is the id for `wakuc surface list|show|hide|toggle|status <name>` (one name may belong to
several windows; a verb applies to all). The host keeps `name -> shown` for names toggled at run time
and gives the windows of the next shell the same state after a reload (`ShellHost::present`).

### The Windows taskbar

`waku --hide-taskbar` (and `wakuc taskbar hide`) hides the Windows taskbar; it is off by default
and inert off the live desktop (`kernel::liveDesktop()`: the offscreen platform has no
`TaskbarSystem`, and the `taskbar` verbs answer an error). Documented APIs only, so no row in the
allowlist below:

- `SHAppBarMessage` `ABM_GETSTATE` reads the state; `ABM_SETSTATE` with `ABS_AUTOHIDE` turns
  auto-hide on, which releases the reserved work area (the call waits at most 0.5 s for the primary
  monitor's work area to be the whole monitor, so the next `ABM_SETPOS` sees it).
- `EnumWindows` finds every `Shell_TrayWnd` and `Shell_SecondaryTrayWnd`; `ShowWindow` with
  `SW_HIDE` (`SW_SHOWNA` to restore) keeps the taskbar away from the Win key and the screen edge,
  which auto-hide alone does not.
- `RegisterWindowMessage("TaskbarCreated")` is received by a hidden top-level window (message-only
  windows get no broadcasts) on the GUI thread, only while hiding; the host then hides the new
  windows again. No thread, no timer, no singleton.

Order: `WindowsTaskbarHider::start` runs after the control pipe is taken (a refused second start
touches nothing) and before the first `ShellHost::reload`, so every app bar is registered after the work
area is released. Before the first change `hide()` writes `taskbar_hidden.marker` in `appDataDir()`
(pid, whether waku turned auto-hide on, whether it hid the windows). `restore()` undoes only those
changes and deletes the file. It runs on `aboutToQuit` (so `wakuc quit` too) and in the
destructor. At the start of every live run a marker whose pid is not a running `waku.exe` is
undone first (a crash, `taskbar /F`); a marker of a live waku is left alone.

At sign-in the startup task can run waku before Explorer has created the taskbar. When `hide()` finds
no taskbar window, it hides them as soon as one appears (the once-a-second check or `TaskbarCreated`)
and updates the marker then.

Co-existence: if another tool (`thide`) already hides the windows or auto-hide is on, waku leaves
that part alone, writes no marker for it and does not undo it. `wakuc taskbar status` reports what
is seen: `hidden` (no taskbar window visible, whoever hid it), `autohide-was-on` (shown, auto-hide
on), `visible`. Taskbar windows Explorer creates later (a monitor plugged in while hiding) stay
visible until the next `TaskbarCreated` or `hide`. Source: `src/host/windows_taskbar.{h,cpp}`,
`win32/windows_taskbar_win.cpp`, `qml/windows_taskbar_command.{h,cpp}`, fakes in
`testing/windows_taskbar_fakes.h`. The names say "Windows taskbar" to keep them apart from the
`taskbar` feature (waku's own task list): `ctest -R WindowsTaskbar` selects these tests.

### komorebi's work area

komorebi caches each monitor's work area and can miss the `WM_SETTINGCHANGE` that follows an app
bar change (its channel drops messages during a dock or scaling storm), so windows tile under the
bar. `KomorebiWorkAreaGuard` (`src/features/workspaces/komorebi_workarea.{h,cpp}`, sender in
`win32/komorebi_workarea_win.cpp`) is one object created and owned by `src/app/waku/main.cpp` for
the run, like `WindowsTaskbarHider`; it is not a singleton and survives reloads. It listens to Qt's
screen signals (`availableGeometryChanged` and friends), waits 500 ms after the last one, then sends
`WM_SETTINGCHANGE(SPI_SETWORKAREA)` to komorebi's hidden window (`FindWindowW`, class
`komorebi-hidden`) with `SendMessageTimeoutW` (250 ms), at once and 5 s later. GUI thread, no thread of its
own (so no row in the thread table), two single-shot timers idle between bursts, documented APIs
only (no allowlist row). `--no-workarea-sync` disables the capability `workarea_sync`; off the live
desktop the guard has no sender. With `--manage-komorebi` (capability `manage_komorebi`) the same
settle also starts `komorebic replace-configuration` after a display change, only while
`komorebi.exe` and `komorebi-bar.exe` run, at most once per 30 s (`CreateToolhelp32Snapshot`, no
allowlist row). Details: `src/features/workspaces/design.md`.

## Threading

Everything that touches Qt Quick objects runs on the GUI thread. Worker threads never touch them; they
hand results back with queued signals or `QMetaObject::invokeMethod`.

| Thread | Owner | Work | Stopped by |
|---|---|---|---|
| `QThreadPool` threads (github) | `github::runAsync` and `github::sendHttpAsync` (`src/features/github/github_api.h`) (notification fetch, mark-as-read, device-flow login) | one fetch (its requests share one `QNetworkAccessManager`) or one blocking QtNetwork request each; the result is posted to qApp and dropped on the GUI thread if its context object is gone (the worker never touches the context) | finishes on its own (15 s transfer timeout); no join needed |
| `QThreadPool` thread (apps scan) | `AppsService` (`src/features/apps/qml/apps_service.{h,cpp}`) | one task per scan: reads the Start Menu `.lnk` files with `IShellLink` and enumerates `shell:AppsFolder`, each in COM's MTA of that thread; posts the shortcuts alone first, then the full catalogue, to the GUI thread. The post checks a mutex-guarded owner pointer, so a result after the service is destroyed is dropped | finishes on its own (a few hundred milliseconds to a few seconds); the destructor clears the owner and does not wait |
| `thumb` provider pool (`QThreadPool`, 3 threads) | `ThumbnailProvider` (`src/host/qml/thumbnails.{h,cpp}`), owned by the QML engine | one `ThumbnailStore::get` per `image://thumb/` request: memory LRU, disk cache, else a scaled `QImageReader` decode and an atomic cache write; the store is thread safe and decodes one entry once; the image returns to the response through a queued signal | the destructor sets a cancel flag (queued loads answer with an error at once) and calls `waitForDone` |
| `Thumbnails` prewarm pool (`QThreadPool`, 2 threads, `LowPriority`) | `waku.Thumbnails` (`src/host/qml/thumbnails.{h,cpp}`) | one `ThumbnailStore::warm` per path of `prewarm()`, in call order, so the first open of a gallery finds its thumbnails cached; one prune of the disk cache (least recently used beyond 200 MB or 2000 files) at creation | `cancelPrewarm()` drops queued work; the destructor clears the queue and calls `waitForDone` |
| `appicon` provider pool (`QThreadPool`, 4 threads) | `AppIconProvider` (`src/features/apps/qml/app_icon_provider.{h,cpp}`), owned by the QML engine | one `kernel::shellItemIcon` call per icon request (joins its thread to COM per call); reads only the mutex-guarded `IconIndex` and the loader; the image returns to the response through a queued signal | the destructor sets a cancel flag (queued loads answer with an error at once) and calls `waitForDone` |
| media session thread (`kernel::TaskThread`, MTA) | `MediaSessionService`'s `SystemMediaSource` (`src/features/media/win32/media_session_winrt.cpp`), created on first use by `waku.services.Media` | owns every GSMTC WinRT object; WinRT handlers only enqueue a task through a `Poster`, the thread sleeps between tasks (no polling) and posts plain snapshots to the GUI thread; position interpolation is a GUI-thread `QTimer` active only while the current session plays | `stop()`: revokes handlers, wakes the queue, `join` |
| taskbar icon loader (`QThreadPool`, one thread) | `TaskbarIcons` (`src/features/taskbar/taskbar_icons.cpp`, owned by `waku.services.TaskbarService`) | `WindowSystem::windowIcon` (WM_GETICON with a 200 ms timeout, shell item images, class icons), serial like Python's `IconService` thread; the image returns to the GUI thread with a queued call and is dropped if the loader is gone | destructor: `waitForDone` (a hung window costs at most the WM_GETICON timeout) |
| `QThreadPool` task (control center first read) | `waku.services.ControlCenter` (`src/features/control_center/qml/control_center_service.cpp`, `initLater`), one task per singleton | reads the user's name and picture, DND, the theme and the power plans and modes through copies of the backend functions; the result returns through qApp and is dropped when the singleton is gone | finishes on its own; nothing to join |
| TabTip launch (`std::thread`, detached) | `toggleTouchKeyboard` (`src/features/control_center/win32/touch_keyboard_win.cpp`), one per toggle that finds TabTip not running | starts TabTip, waits up to 5 s for its message pump (`WaitForInputIdle`) and toggles it through its COM server; owns nothing of ours, initialises COM for itself | ends after the toggle or the wait; not joined |
| `network` pool (`QThreadPool`, 2 threads) | `networkPool()` (`src/features/network/win32/network_pool.{h,cpp}`, parented to qApp), used by `waku.services.Network` (`radio_win.cpp`, `vpn_win.cpp`) | one task per call, off the global pool: Wi-Fi radio read and switch (`Windows.Devices.Radios`), RAS list, dial and hang-up (`RasDial` blocks until the dial ends); results return as queued signals and are dropped if the backend is gone | a task finishes on its own; the pool is deleted with qApp, which waits for running tasks |
| network change callbacks (`SystemChangeWatcher`) | `waku.services.Network` (`src/features/network/win32/change_watcher_win.cpp`), created at `componentComplete` | `NetworkInformation::NetworkStatusChanged` runs its handler on a WinRT pool thread and the `WlanRegisterNotification` callback (ACM and MSM sources) on a wlanapi thread; both only post `changed` to the GUI thread through `postToGui` and touch nothing else. The GUI thread owns the watcher; its destructor removes both registrations before the object goes | removed in `~SystemChangeWatcher`; a late callback finds an empty guard and does nothing |
| `WlanBackend` (`QThread`) | `waku.services.Network` (`src/features/network/qml/network_service.{h,cpp}`, backend `src/features/network/win32/wlan_win.cpp`) (created on the first refresh or connect) | wlanapi scan, connection query, profile edits, `Windows.Devices.WiFi` connect and disconnect; results return as queued signals | `~WlanBackend` does not wait: it queues `shutdown()` (unregister notifications, close the handle) and `quit`; the GUI loop deletes the thread and the worker on `finished`, so a connect in progress no longer blocks a reload; at exit a post routine waits up to 2 s per unfinished thread |
| wallpaper overlay decoder (`QThread`) | `WallpaperTransition` (`src/features/wallpapers/qml/wallpaper_transition.h`, one per transition) | decodes the new image and the transcoded copy of the old one; touches no Qt Quick object, posts the images back to the GUI thread through a checked `QPointer` | the thread deletes itself when it ends; `cancel()` never waits for it, the transition's destructor `wait()`s for a running one (at most one decode) so none outlives the application |
| lock screen worker (`kernel::TaskThread`, MTA) | `WinrtLockScreenSetter` (`src/features/wallpapers/win32/lock_screen_win.cpp`), owned by `waku.services.Wallpapers` on a live desktop, started by the first change with `setLockScreen` on | owns the WinRT `StorageFile` and `LockScreen.SetImageFileAsync` calls and the PNG copy (`%LOCALAPPDATA%\waku\lock_screen.png`) of an image Windows does not take as is; `set()` only stores the latest path under a mutex and arms one coalesced apply; a failure is one warning line; touches no Qt object besides `QImage` | destructor: `stop()`, which joins (waits for one WinRT call at most) |
| `RealBluetoothBackend` (`QThread`) | `waku.services.Bluetooth` (see singletons) | BluetoothAPIs and WinRT enumeration, battery reads, radio power, device watchers, audio connect/disconnect, scan, pair and forget (`DeviceInformationPairing`; blocks the thread like a connect); results return as queued signals. WinRT delegates (radio, device watcher, connection status) run on WinRT pool threads and reach the worker only through a mutex-guarded relay that `~BluetoothWorker` closes before the worker is freed | destructor: `stopSoon()` (atomic flags: a running pair, connect, refresh, LE listing or enrichment ends after the WinRT call in progress), then queues `shutdown` and `quit` without waiting; the GUI event loop deletes the thread and the worker on `finished`, and a post routine waits up to 2 s per unfinished thread at exit; a running pair also ends at `cancelPairing()` (polled every 100 ms) or after 60 s |
| GlazeWM WebSocket thread (`std::thread`) | `GlazewmService` (`src/features/workspaces/glazewm_service.h`) via `glazewm::GlazewmSocket` (`src/features/workspaces/glazewm.h`, `win32/glazewm_socket.cpp`), created by the first widget subscription | WinHTTP session, connect, blocking receive loop, reconnect sleep (4 s, doubling to 30 s while GlazeWM does not listen); the only thread that closes the WinHTTP handles while it runs (closing under a synchronous call is not allowed); frames and connection changes reach the GUI thread as queued signals | `GlazewmSocket::stop()` (also from the destructor): stop flag and event, `WinHttpWebSocketClose` to cancel a blocked receive (at most a 250 ms close handshake), no `join`: the thread is detached and holds the shared state; a connect in progress ends within its 2 s timeout, then the worker closes its handles and ends; nothing is emitted after `stop()`; at exit a post routine waits up to 2 s per worker. A localhost URL is connected to only while `GetExtendedTcpTable` lists a listener on its port, so no connect waits on a refused port while GlazeWM is not running |
| komorebi pipe thread (`std::thread`) | `KomorebiService` (`src/features/workspaces/komorebi_service.h`) via `komorebi::KomorebiPipeListener` (`src/features/workspaces/komorebi.h`, `win32/komorebi_pipe.cpp`) | owns the `\\.\pipe\waku-<uuid>` pipe, runs `komorebic subscribe-pipe` and `state` (`CreateProcessW` with drained output pipes, killed when the stop event fires), reads one JSON message per pipe message, reconnects; checks for `komorebi.exe` every 5 s, doubling to 30 s while it is absent; state reaches the GUI thread as queued calls | `stop()`: stop event (cancels waits, pipe I/O and a running komorebic), `join`, then `komorebic unsubscribe-pipe` started detached |
| systray hook thread (`std::thread`) | `SystrayHook` (`src/features/tray/win32/tray_hook.h`), owned by `waku.services.Systray`, started by the first Tray widget | injects `WakuTrayHook[_arm64].dll` into Explorer, owns the pipe I/O, parses each message, scales non-32 px icons and emits queued signals to the GUI thread; retries after Explorer restarts; its I/O waits block on the completion event and a stop event (no polling) | `stop()` (also from the destructor): stop flag, set the stop event, release the hook, cancel I/O, close the watchdog mutex, `join` |
| `BrightnessControl` worker (`std::thread`) | `waku.services.ControlCenter`, created on first use (`src/features/brightness/win32/brightness_win.cpp`) | owns DDC/CI reads and writes, display metadata (WinRT `DisplayMonitor`) and the coalesced slider writes; results return as queued signals; DDC probes fan out with `std::async` and are joined by the worker | `stop()` (called by `stopAll()`): sets the stop flag, wakes the queue, `join` |
| `BrightnessControl` policy thread (`std::thread`, `policyThread_`) | the same `SystemBrightness`, started with it | owns a message-only window and the power-setting registration; handles brightness changes in `onPolicyChange`; fails soft (no watcher). Not waited for at start | `stopPolicyWatcher()`: stop flag, `WM_CLOSE`, `join`; a thread that publishes its window after the flag closes it itself |
| notifications worker (`kernel::TaskThread`, MTA) and WNF callback | `NotificationService`'s `SystemNotificationSource` (`src/features/notifications/win32/notifications_system.cpp`), created on first use by `waku.services.Notifications` | the worker owns the `UserNotificationListener` and runs list reads (100 ms debounce), remove, clear and toast activation tasks (the read is its coalesced `postDeferred`), sleeping between them; the WNF callback runs on a system thread and arms the debounced read | `stop()`: unsubscribe WNF (`WnfSubscription`, `win32/wnf_win.h`: the context is a registry token and the handler runs under a per-subscription mutex that `unsubscribe()` takes, so it returns after a running callback has left), wake the queue, `join` |
| `DndWatcher` WNF callback (system thread) | `DndWatcher` (`src/features/notifications/win32/dnd_win.cpp`), owned by `waku.services.ControlCenter` | the callback reads `dndStatus()` and posts the result to the GUI thread (`statusChanged`) | `stop()`: unsubscribe WNF through `WnfSubscription` (`win32/wnf_win.h`), which returns after a running callback has left (at most one COM call) |
| `shellOpen` thread (`std::thread`, STA, detached) | `kernel::shellOpen` (`src/kernel/win32/shell_launch.cpp`), one per call; callers are `Shell.open`, `ControlCenter` and toast activation | initialises COM (apartment-threaded), calls `ShellExecuteW` "open" and logs a failure; touches no Qt object, so a slow or blocking handler never stalls the GUI thread | none: the thread ends with the call and nobody joins it, so a call in flight when the process exits is cut off |
| `shellRunElevated` thread (`std::thread`, STA, detached) | `kernel::shellRunElevated` (`src/kernel/win32/shell_launch.cpp`), one per call; callers are the apps launcher (`launchAsAdmin`, Run as administrator) and `execDetached` with `elevated` | initialises COM (apartment-threaded), calls `ShellExecuteEx` with the `runas` verb and logs a failure or a declined UAC prompt; touches no Qt object. It runs off the GUI thread because the call blocks until the prompt is answered and pumps window messages, which on the GUI thread let the launcher close under the call and crashed the bar | none: the thread ends with the call and nobody joins it |
| `QThreadPool` task (color_scheme) | `ColorSchemeService::fromImage` (`src/features/color_scheme/qml/color_scheme_service.{h,cpp}`), one task per call | decodes a reduced copy of the image, quantizes it and builds the scheme; touches no `QObject`, posts the `SchemeValue` to qApp, where a `QPointer` to the service is tested on the GUI thread | the task ends on its own; a service destroyed first leaves the `QPointer` null and the result is dropped |
| `QThreadPool` threads (quickshell_io) | `FileView` (`src/features/quickshell_io/qml/file_view.{h,cpp}`), one object per QML `FileView` | one `FileAccess::read` or `write` per job (QFile, QSaveFile; the access is thread-safe and holds no Qt Quick object); the result returns through a `QFutureWatcher` owned by the `FileView`, so a result after the object is gone is dropped; writes run one at a time | nothing to join: each job ends with its file operation |

Platform data sources (`src/features/*/win32/`) are sampled off the GUI thread by the stat sources;
anything they cache globally must be locked.

## Singletons and process-wide state

No new singleton without a row here and a reason.

| What | Where | Notes |
|---|---|---|
| working set trim | `src/host/qml/shell_host.*`, `src/host/win32/shell_host_win32.cpp` | `ShellHost::install()` starts a single-shot GUI-thread `QTimer` (5 s); when it fires and the window count has not grown since the previous look (install, then each tick) it calls `NativeHooks::trimWorkingSet` (`SetProcessWorkingSetSizeEx(-1, -1)`, documented API), else it waits another 5 s. A shell that shows windows late (noctalia) is trimmed once they stop appearing. Live desktop only. Idle working set of the sample shell 114 MB to 29 MB, no climb back in 60 s |
| start-up trace clock | `src/host/startup_trace.{h,cpp}` | one `QElapsedTimer` started at the top of `main()` and a flag read once from `WAKU_TRACE`; `traceStep()` logs the milliseconds since start for each start-up and reload step. GUI thread only; does nothing unless `WAKU_TRACE=1` |
| media source-app name cache, shell-app cache | `src/features/media/win32/media_source_apps.cpp` (names), `src/kernel/win32/app_activation.cpp` (shell-app cache) | AUMID to display name; `QMutex`-guarded, written from the media session thread only |
| `waku.services.Systray` | `src/features/tray/qml/systray.{h,cpp}` | the one tray host of a `waku` run (the hook in Explorer), shared by every Tray widget of the bar; created by the QML engine on first use and destroyed with it, which stops the host. |
| `waku.services.WindowControl` | `src/features/taskbar/qml/window_control.{h,cpp}` | `activate(hwnd)`, `foreground()` and `isWindow(hwnd)` for HWNDs that come from elsewhere (komorebi state). Stateless: a thin call into Win32 through a `WindowControlBackend`. GUI thread only; no thread, no timer. Inert (false, 0) without a live desktop |
| app catalogue cache (`AppCatalog`) | `src/features/apps/qml/apps_service.{h,cpp}` (`acquireAppCatalog`, a `kernel::LingeringHub`) | the last complete Start Menu and `shell:AppsFolder` catalogue and the one `QFileSystemWatcher` on the Start Menu folders, shared by the `AppsService` of every engine and kept 30 s past its last holder, so the service a reload builds starts from it without a scan; a folder change makes it stale and the current service rescans. GUI thread only |
| kept WLAN session | `src/features/network/win32/wlan_win.cpp` (`currentSignalQuality`), `connectivity_win.cpp` (`localIpAddress`) | the status poll keeps one WLAN client handle open between polls (`std::mutex`, polls run on pool threads; closed after a read without a quality, so a disconnect or a WLAN service restart reopens it; closed at exit) |
| `waku.services.Network` | `src/features/network/qml/network_service.{h,cpp}` | the one Wi-Fi/Ethernet/VPN state of a `waku` run, shared by every Wifi widget; created on first use, destroyed with the engine. Samples on the global `QThreadPool` when Windows reports a change and every 60 s as a safety net (5 s without change notifications); reads the radio and the VPN list only while `wifiChanged` / `vpnChanged` have an observer; the WLAN backend thread starts on the first refresh or connect and stops in the destructor. No source on the offscreen platform |
| `Quickshell.Services.UPower.UPower`, `PowerProfiles`; `Quickshell.Services.Pipewire.Pipewire`; `Quickshell.Services.SystemTray.SystemTray`; `Quickshell.Bluetooth.Bluetooth`; `Quickshell.Networking.Networking`; `Quickshell.Wayland.ToplevelManager`; `Quickshell.Hyprland.Hyprland` (and the enum singletons beside them) | `src/quickshell/{services/upower,services/pipewire,services/systemtray,bluetooth,networking,wayland,hyprland}/*.qml` | QML singletons, as in Quickshell. Each reads the waku service singletons through one replaceable `backend`, `service` or `output` property that tests set to a fake; none owns a thread or a native resource. Created on first use, GUI thread only |
| `waku.services.QuickshellSingleton` | `src/features/quickshell_io/qml/quickshell_singleton.{h,cpp}` | Quickshell's window-free `Quickshell` members (env, execDetached, shellDir and the state, cache and data folders, clipboardText, iconPath stub, reload request signal) for QML written for Quickshell. A QML singleton because the real one is; GUI thread only, created on first use, holds the shell folder the host sets. The `Quickshell` singleton of the `Quickshell` module (`src/quickshell/core/qml/Quickshell.qml`) forwards to it and adds `screens` |
| `waku.services.AppsService` | `src/features/apps/qml/apps_service.{h,cpp}` | the one app catalogue of a `waku` run (Start Menu shortcuts and Store apps as `DesktopEntry`, ranked search, launch, frecency in `%LOCALAPPDATA%\waku\apps_frecency.json`); created on first use, destroyed with the engine (`create()` registers `image://appicon`). Scans on a pool task (see threads); a `QFileSystemWatcher` rescans after 1.5 s of quiet. Inert unless `kernel::liveDesktop("apps")` |
| `fileAccess()` override | `src/features/quickshell_io/file_access.cpp` | `std::atomic<FileAccess*>`, null in production; only tests set it (a fake disk), before the first `FileView` is created |
| WNF subscription registry | `src/features/notifications/win32/wnf_win.cpp` (`WnfSubscription`) | map from the integer token WNF gets as its callback context to the live subscription, behind a `std::mutex`; a callback for a token no longer in it does nothing, so WNF never holds a pointer to an object a reload destroys |
| cached Windows build number | `src/kernel/win32/windows_build.cpp` (`windowsBuild()`) | immutable |
| live-desktop disable flags | `src/kernel/qt/live_desktop.cpp` | set of capability names behind a `QMutex`; written once at startup (`--no-tray`), read by the services' real constructors |
| app data migration record | `src/kernel/qt/app_data.cpp` (`appDataDir`) | set of base folders already migrated in this process, behind a `QMutex`; the on-disk marker `migrated_from_yasb` makes the copy happen once per machine |
| `waku.services.TaskbarService` | `src/features/taskbar/qml/taskbar_service.{h,cpp}` | the one window tracker, pinned-apps store (`taskbar_pinned.json`) and window-icon cache of a `waku` run, shared by every Taskbar widget; created on first use, destroyed with the engine (tracker stopped, icon pool joined). Registers `image://windowicon`. The tracker starts when the first Taskbar widget hands over its bar window; never on the offscreen platform |
| `waku.services.GitHub` | `src/features/github/qml/github_service.{h,cpp}` | the GitHub notifications of a `waku` run for every GitHub widget. Polls on a `QTimer`; each fetch is a one-shot pool task (see threads) whose result is posted to qApp and runs only while the client's guard object is alive, so a response after destruction is dropped. Token from `WAKU_GITHUB_TOKEN` or the token file, kept in memory only. No client on the offscreen platform |
| `waku.services.Wallpapers` | `src/features/wallpapers/qml/wallpapers_service.{h,cpp}` | the wallpaper listing, current wallpaper, gallery state and toggle hotkey of a `waku` run, shared by every Wallpapers widget; created on first use, destroyed with the engine; thumbnails are the host's `image://thumb/`. Wraps the UI-free `WallpaperManager` and `DesktopWallpaperBackend`. Its one thread is the lock screen worker (see threading); the hotkey is a handle in `kernel::Hotkeys` (GUI thread), removed in the destructor. Inert, no hotkey, unless `kernel::liveDesktop("wallpapers")`; no hotkey either without `kernel::liveDesktop("hotkeys")` (`--no-hotkeys`), which `kernel::Hotkeys` asks |
| `waku.services.ColorScheme` | `src/features/color_scheme/qml/color_scheme_service.{h,cpp}` | Material 3 colour generator (`fromColor`, `fromImage`, `generate`, `luminance`) over the pure `color_scheme` engine; created on first use, destroyed with the engine. Holds one `ImageStatsCache` (`%LOCALAPPDATA%\waku\image_stats.json`, 256 entries, atomic writes; memory only unless `kernel::liveDesktop("color_scheme")`). `fromImage` runs on a pool task (see threads). Reads only the file the caller names |
| `waku.services.Bluetooth` | `src/features/bluetooth/qml/bluetooth_service.{h,cpp}` | the one Bluetooth backend of a `waku` run, shared by every Bluetooth widget; created on first use, destroyed with the engine, which stops the backend thread without waiting for it. No backend unless `kernel::liveDesktop("bluetooth")` and an adapter exists. Two GUI-thread `QTimer`s: a single-shot retry while the radio is unknown and a 60 s refresh that runs only while a binding observes `changed` |
| `waku.services.DesktopState` | `src/features/desktop_state/qml/desktop_state.{h,cpp}` | caps, num and scroll lock, keyboard layout and idle seconds, created by the QML engine as a singleton on the GUI thread. A `QTimer` (200 ms with a lock key or layout observer, 1 s with only `idleSecondsChanged`) runs only while a binding or connection observes one of its change signals (`connectNotify`/`disconnectNotify`); no thread. Reads nothing without a live desktop |
| `desktop_state::systemIdleInhibitCounter()` | `src/features/desktop_state/win32/desktop_probe_win.cpp` | the count of enabled `IdleInhibitor` objects over one `SetThreadExecutionState` state (it acts on the calling thread, so only the GUI thread uses it); function-local static, released when the count reaches 0 and at exit. No probe without a live desktop |
| `waku.services.AudioStreams` | `src/features/audio/qml/audio_streams.{h,cpp}` | the per-app audio sessions of the default output (`IAudioSessionManager2` through `waku::audio::AudioStreamSource`), created by the QML engine as a singleton on the GUI thread; owns its source, which releases the Core Audio callbacks in `stop()` from the destructor. No thread: the COM callbacks only re-post to qApp, and the source is tested through a `QPointer` on the GUI thread (a callback can outlive a failed `Unregister`). Has no source without a live desktop |
| `waku.services.Mpris` (and the `MprisPlayer` objects it owns) | `src/features/media/qml/mpris.{h,cpp}` | every GSMTC session as a Quickshell-shaped player list, created on first use. Shares the process's one `MediaSessionService` with `Media` through `acquireMediaService()` (a `kernel::LingeringHub` in a function-local static, a `shared_ptr` per holder); the service outlives its last holder by 30 s, so a reload finds it running; the session thread is joined when that time is up without a new holder |
| `waku.services.Notifications` | `src/features/notifications/qml/notification_service.{h,cpp}` | the Action Center toasts for every Notifications widget of a `waku` run, created by the QML engine as a singleton; wraps `waku::notifications::NotificationService` from `acquireNotificationService()` (a `kernel::LingeringHub`: the WNF subscription and the listener worker outlive the last holder by 30 s, so a reload finds them running; stopped and joined when that time is up without a new holder); has no source on the offscreen platform |
| `waku.services.NotificationFeed` (with `NotificationServer` and the `Notification` objects, each property with its `...Changed` signal, and `NotificationAction`) | `src/features/notifications/qml/notification_feed.{h,cpp}` | the Action Center toasts as a Quickshell-shaped model with `toastAdded`, created on first use. A `NotificationHub` (`weak_ptr` from `acquireNotificationHub()`) owns the toast objects and shares the one `NotificationService` with `Notifications`; the listener thread is joined when the last holder goes |
| `waku.services.ControlCenter` | `src/features/control_center/qml/control_center_service.{h,cpp}` | the system side of every ControlCenter widget: Do Not Disturb (its own `DndWatcher`), app theme, touch keyboard, power plans, user name and picture, session and power actions behind an injectable launcher. Owns one `BrightnessControl` (worker and policy threads, joined by `stop()` in the destructor). Volume, microphone and player come from `AudioOutput`, `AudioInput` and `Media`. Inert on the offscreen platform |
| `kernel::Hotkeys` | `src/kernel/qt/hotkeys.{h,cpp}`, backend `src/kernel/win32/hotkeys_win.cpp` | the global hotkeys of a `waku` run, shared by every `GlobalShortcut` and the wallpaper gallery: maps key combinations to callbacks (two users on one combination share one registration and both fire; a combination another program owns is refused and logged once as taken) and owns the one `HotkeyBackend`. A singleton because Windows scopes `RegisterHotKey` ids to the thread and one table keeps the ids unique. GUI thread only. The backend is built on the first `add`; every id is unregistered in the destructor at the latest. Inert unless `kernel::liveDesktop("hotkeys")` (`--no-hotkeys`). Tests install a fake (`src/kernel/testing/hotkeys_fixtures.h`) |
| `waku.Thumbnails` | `src/host/qml/thumbnails.{h,cpp}`, store `src/host/thumbnail_store.{h,cpp}` | `url`, `prewarm` and `cancelPrewarm` for a gallery of large images, and the owner of the `image://thumb/` provider (registered by `create()`). Holds the process-wide `ThumbnailStore` (a `weak_ptr` hub shared by every engine generation and its provider: memory LRU of 8 MB, disk cache `%LOCALAPPDATA%\waku\thumbs`) and the prewarm pool. GUI thread only; created on first use. Memory only unless `kernel::liveDesktop("thumbnails")` |
| `waku.services.Workspaces` | `src/features/workspaces/qml/workspaces_service.{h,cpp}` | one `monitors` model over `Komorebi` and `GlazeWM`; `prefer` picks the provider when both are connected. `create()` borrows the engine's two client singletons with `singletonInstance`, so it is rebuilt with them after a reload. GUI thread only; no thread, timer or native resource. Provider is empty when neither manager runs |
| `waku.services.Komorebi` | `src/features/workspaces/qml/komorebi_client.{h,cpp}` | the one komorebi state of a `waku` run, shared by every widget and by `Workspaces`; created by the QML engine on first use. Owns a `KomorebiService`, which starts the pipe listener thread (see threads) at construction; the destructor calls `stop()`, which joins it. Commands run as hidden processes. Inert unless `kernel::liveDesktop("komorebi")` (no listener, no command); `connected` stays false while komorebi is not running |
| `waku.services.GlazeWM` | `src/features/workspaces/qml/glazewm_client.{h,cpp}` | the one GlazeWM state of a `waku` run, shared by every widget and by `Workspaces`; created by the QML engine on first use. Owns a `GlazewmService` that subscribes to three topics at construction and starts the WebSocket thread (see threads); the destructor calls `stop()`, which does not wait for it. Inert unless `kernel::liveDesktop("glazewm")` (no socket); `connected` stays false while GlazeWM is not running |
| `waku.services.Media` | `src/features/media/qml/media_service.{h,cpp}` | the current GSMTC session for every Media widget, created by the QML engine on first use. Holds the process's one `MediaSessionService` from `acquireMediaService()`, shared with `Mpris` (see that row); it re-emits the service's signals as `changed`. GUI thread only; the session thread is in the threads table and is joined 30 s after the last holder is destroyed unless a new one comes (a reload). No GSMTC source unless `kernel::liveDesktop("media")`: `available` stays false |
| `waku.services.AudioOutput`, `waku.services.AudioInput` | `src/features/audio/qml/audio_service.{h,cpp}` | the default render and capture endpoint (volume, mute, devices, default device), one singleton each, created by the QML engine on first use. Each owns one `AudioEndpoints` (`createSystemAudio`); its Core Audio callbacks only re-post to the GUI thread. The destructor calls `stop()`, which releases the callbacks. No thread of its own. No endpoint unless `kernel::liveDesktop("audio")`: nothing available, writes ignored |
| `waku.services.Shell` | `src/features/quickshell_io/qml/shell_service.{h,cpp}` | what a widget does on click: `start`, `command`, `open`, `env`, `homeDir`, `configHome`. Stateless apart from the config home and an injectable `Launcher` (tests replace it, so no test starts a program). GUI thread only; `open()` goes to `kernel::shellOpen`, which uses a detached thread (see threads). Does not ask `kernel::liveDesktop()` |
| `waku.Theme` | `src/host/qml/theme.h` | the theme values widgets read, one per engine. `ShellHost::applyTheme` fills it from `theme.qml` (or the defaults with `--safe`) before each build and after each reload. GUI thread only; no thread, timer or native resource |
| `waku.PopupPlacement` | `src/host/qml/popup_placement_qml.h` | the QML face of the pure `placePopup()` and the edge helpers, so `PopupWindow` and `Tooltip` have an object to call. Stateless; GUI thread only; no thread, timer or native resource |
| `waku.ScreenList` | `src/host/qml/shell_screen.{h,cpp}` | the screens shells are built for, one per engine (QML singleton). `ShellHost` replaces its `ShellScreen` objects before each build (tests give it any screens); without a host it holds the application's screens. `Quickshell.screens` forwards to it |
| context property `ShellHost` | `src/host/qml/shell_host.cpp` | the one `ShellHost` of an engine, on its root context; every `QsWindow` registers with it on completion (`ShellHost::of`). Cleared when the host is destroyed |

Function-local `static const` regexes and lookup tables are immutable and not listed.

### Singletons across a reload

`ShellHost::reload` starts a clean engine, as Quickshell does. After the check of the new files
passes, it destroys the old shell, calls `QQmlEngine::clearSingletons()` and `clearComponentCache()`,
emits `singletonsCleared` and builds the new shell. Without it, a QML singleton kept its old type
definitions and the new shell failed with `Unable to assign Foo to Foo` and null dereferences. Rules:

- Every singleton the engine owns is destroyed and created again on first use. Nothing may keep a
  pointer to one across a reload: resolve it with `singletonInstance` at the time of use. The
  composition root does so (`wallpaper` accessor in `main.cpp`, `connectQuickshell` rewires the
  `Quickshell` singleton on `singletonsCleared`). A shell part that caches one (`WallpaperSettings`)
  is destroyed with the old shell and resolves it again in `activate()`.
- A shell that passes the check but cannot be created leaves the fallback shell with the error: the
  old shell is gone. A new shell that creates no window while the old one had some (a `Variants`
  delegate that failed) counts as a failure too: the error is `the shell created no windows` plus
  the first QML warning of the build, and `reload()` returns false. A first start with no windows
  stays valid. A file that fails the check still keeps the old shell and the singletons.
- State that must survive stays outside the singletons: `PersistentProperties` (per engine), the
  taskbar marker, `kernel::Hotkeys`, and the shared hubs (`kernel::LingeringHub`: the media and
  notification services, the system_stats data sources), which live while any holder lives and
  30 s after the last one goes, so a reload does not stop and start them. A hub holds no QML
  object and nothing of the engine: the singletons that expose it are new after every reload.
- Each reload therefore stops and starts the services the new shell uses: the `Systray` host (the
  hook in Explorer is detached and injected again, so tray icons flicker), the
  `TaskbarService` tracker (it restarts when a Taskbar widget hands over its bar window),
  `Bluetooth`, `Network`, `ControlCenter` (brightness worker), `Wallpapers` (its hotkey and
  watchers). Each destructor stops its thread (threading table); none waits on the GUI thread for
  a WinRT or network call in progress (Bluetooth, Wi-Fi, GlazeWM hand the thread to the event loop
  or detach it). The image
  providers they register (`tray`, `appicon`, `windowicon`, `wallpaper`, `thumb`) belong to the engine and
  are replaced when the singleton is created again. Until then the old provider holds the model
  (`QPointer`) or the shared index or cache, so it answers with a null image and never touches a
  destroyed service. A provider must keep it so: no raw pointer to the singleton.

## Error handling

- Exceptions never cross a Qt boundary: slots, lambdas connected to signals, event handlers,
  `paintEvent`, timers and any callback invoked by Qt. An exception escaping into Qt or
  `QGuiApplication::exec()` is `std::terminate` with no log line.
- Every entry point from Qt into our code either uses non-throwing APIs or wraps its body in
  `try`/`catch` and logs with `qWarning`/`qCritical`.
- API boundaries return values, `std::optional` or a result struct. Internal exceptions thrown and
  caught within one module are fine (`src/host/win32/backdrop.cpp`).
- Never use the `const` `nlohmann::json::operator[]` on a key that may be missing; the
  `json-subscript` step of `tools\check.ps1` looks for it.

## Undocumented Windows API allowlist

Every undocumented API, hand-laid struct or numeric constant missing from the SDK is listed here
with the build it was verified on and its fallback. Add a row in the same commit that adds the call.
Each call fails soft. A row that says "not exercised live" has a line in
`docs/release-checklist.md`, which says how to check it by hand and when it last worked.
`IDesktopWallpaper` (`src/features/wallpapers/win32/desktop_wallpaper_win.cpp`)
is documented and needs no row. Neither does `user32!EndTask`
(`src/features/taskbar/win32/window_system_win.cpp`): it is documented, loaded by name because the
import library lacks it. The taskbar, brightness and Bluetooth audio code take their
SDK constants (`PKEY_AppUserModel_ID`, `GUID_VIDEO_SUBGROUP`, `PKEY_Device_FriendlyName`) from the
SDK headers instead of spelling them out.

| API | Where | Verified on | Fallback |
|---|---|---|---|
| `ntdll!NtQuerySystemInformation` class 80 (`SystemMemoryListInformation`), local `MemoryListInformation` struct | `src/features/system_stats/win32/memory.cpp` (`cachedBytes`) | Windows 11 build 26300, ARM64 | `GetPerformanceInfo` `SystemCache` when the symbol is missing or the call fails |
| Message `0x052C` (`WM_SPAWN_WORKER`) sent to `Progman` with `SendMessageTimeout`, to make Explorer create the WorkerW window behind the desktop icons; then `SetParent` of a Qt top-level window onto that WorkerW (dropping and restoring `WS_EX_LAYERED` around the call); `detachFromWorkerW` makes it a top-level window again before it is deleted, and does nothing when an Explorer restart already destroyed it (`IsWindow`) | `src/features/wallpapers/win32/workerw.cpp` (`attachToWorkerW`, `detachFromWorkerW`) | Python YASB on Windows 11 build 26100; not exercised live ([release checklist](release-checklist.md)) on build 26300 in this port | no `Progman` or no WorkerW found: `attachToWorkerW` returns nullopt, the transition overlay is skipped (logged once), and the wallpaper is set without animation |
| Registry values `HKCU\Control Panel\Desktop`: `MaxVirtualDesktopDimension`, `MaxMonitorDimension` (DWORD), `PanoramaThreshold`, `PanoramaPortraitThreshold` (DWORD, thousandths), `TileWallpaper` / `WallpaperStyle` (strings), `HKCU\Control Panel\Colors\Background`, and the file `%APPDATA%\Microsoft\Windows\Themes\TranscodedWallpaper` | `src/features/wallpapers/win32/workerw.cpp` (`readWallpaperFit`, `readTranscodeLimits`, `readPanoramaThreshold`, `readDesktopBackgroundRgb`, `transcodedWallpaperPath`) | Python YASB (`engine.py`) on Windows 11 build 26100; not exercised live ([release checklist](release-checklist.md)) on build 26300 in this port | a missing or unreadable value uses Python's default (fit "fill", threshold 2.22 / 1.0, black background, no transcode resize); an unreadable transcoded copy skips the animation |
| `ntdll!NtQueryInformationProcess` class 60 (`ProcessCommandLineInformation`), read as a `UNICODE_STRING` | `src/features/taskbar/win32/app_identity_win.cpp` (`hostArguments`) | key and layout from Python YASB (`pin_context.py`) | symbol missing or call failing gives no host arguments; `mmc.exe` / `rundll32.exe` windows then share one `path:` identity |
| `ITipInvocation` ({37C994E7-432B-4834-A2F7-DCE1F13B834B}) from `CLSID_UIHostNoLaunch` ({4CE576FA-83DC-4F88-951C-9D0782B4E376}) `Toggle(HWND)` | `src/features/control_center/win32/touch_keyboard_win.cpp` | ids from Python YASB; not exercised live ([release checklist](release-checklist.md)) on build 26300 | server not reachable: start `TabTip.exe` with `ShellExecuteEx`, `WaitForInputIdle`, toggle again; a missing TabTip logs a warning |
| `powrprof!PowerGetUserConfiguredACPowerMode`, `PowerSetUserConfiguredACPowerMode`, `PowerSetUserConfiguredDCPowerMode` (power-mode overlays; absent on older Windows 10) and the overlay GUIDs 961cc777-2547-4f9d-8174-7d86181b8a7a / ded574b5-45a0-4f42-8737-46345c09c238 | `src/features/control_center/win32/power_win.cpp` (`activePowerMode`, `setPowerMode`) | Windows 11 build 26300, ARM64 (`PowerGet...` verified live: `Balanced`; the setters are not exercised live ([release checklist](release-checklist.md))) | resolved with `GetProcAddress`; a missing getter falls back to the `ActiveOverlayAcPowerScheme` registry value, a missing setter returns false and the mode label keeps the chosen name; on Windows 10 the mode button is not built |
| `SHELLTRAYDATA` / 32-bit `NOTIFYICONDATA` layout (964 bytes: signature, message, then the 956-byte struct with 32-bit handles) that Explorer sends in `WM_COPYDATA` `dwData` 1 to `Shell_TrayWnd`; `dwData` 3 `WINNOTIFYICONIDENTIFIER` | `src/features/tray/tray_pipe.cpp` (`parseShellTrayData`), `src/features/tray/hook/trayhook.cpp` | layout from Python YASB 2.0.8; the parser is checked against an independently laid out struct in `src/features/tray/tests/test_systray_pipe.cpp`; run live on Windows 11 build 26300, ARM64, 2026-10-29 (14 icons arrived, clicks reached the apps) | a short or malformed message is dropped with a warning; an unknown shell message type is ignored |
| Injecting a DLL into `explorer.exe` with `SetWindowsHookEx(WH_GETMESSAGE)` on the `Shell_TrayWnd` thread, which then subclasses `Shell_TrayWnd` (`SetWindowLongPtr(GWLP_WNDPROC)`) and writes tray messages to a named pipe | `src/features/tray/win32/tray_hook.cpp` (`ExplorerInjector`), `src/features/tray/hook/trayhook.cpp` | design and DLL source from Python YASB 2.0.8; run live on Windows 11 build 26300, ARM64, 2026-10-29 (Explorer restart recovers; `wakuc quit` and a forced kill detach the DLL within 1 s) | `createTrayHost` returns no host (the tray stays empty) when the DLL is missing, the architecture is unsupported, the process runs under emulation or the pipe and mutex cannot be created; a failed injection retries every 5 s; the DLL detaches itself within 5 s when the host's watchdog mutex is released or abandoned, restoring the saved window procedure |
| `DEVPKEY_Bluetooth_Battery` ({104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2), a PnP property key read with `CM_Get_DevNode_PropertyW` | `src/features/bluetooth/win32/bluetooth_win.cpp` (`readBattery`) | Windows 11 build 26300, ARM64 (key and layout from Python YASB) | read failure or non-byte type gives no battery value; the menu then shows no battery |
| `KSPROPSETID_BtAudio` ({7FA06C40-B8F6-4C7E-8556-E8C33A12E54D}) one-shot ids 0 (reconnect) and 1 (disconnect) sent through `IKsControl::KsProperty` | `src/features/bluetooth/win32/bluetooth_audio_win.cpp` (`setAudioConnection`) | key and ids from Python YASB; not exercised live ([release checklist](release-checklist.md)) on build 26300 | no KS filter or a refusing driver returns `no_ks_controls` / `oneshot_failed`; the menu shows "Failed to connect/disconnect device" |
| `ntdll!RtlSubscribeWnfStateChangeNotification` / `RtlUnsubscribeWnfStateChangeNotification` (resolved with `GetProcAddress` in `src/features/notifications/win32/wnf_win.cpp`, `WnfSubscription::Api`; the callback context is an integer token, not a pointer; nothing documents that the unsubscribe call waits for a running callback, so `WnfSubscription` does not rely on it) with WNF state names `WNF_SHEL_NOTIFICATION_TOTAL` (0x0D83063EA3B8D035, Windows 11 toast total) and `WNF_SHEL_NOTIFICATIONS` (0x0D83063EA3BC1035, Windows 10 unread badge); the buffer is read as a `u32` count | `src/features/notifications/win32/notifications_system.cpp` | Windows 11 build 26300, ARM64 (names, signatures and buffer layout from Python YASB) | a missing symbol or a non-zero status for both names logs a warning and leaves the count at 0 (icon `default`); clearing still works through the documented `UserNotificationListener` |
| `IPolicyConfig` ({f8679f50-850a-41cf-9c72-430f290290c8}) from `CLSID_PolicyConfigClient`, hand-laid vtable (`SetDefaultEndpoint` at slot 10 after `IUnknown`) | `src/features/audio/win32/audio_endpoints_win.cpp` (`setDefaultDevice`) | layout and ids from pycaw (same call Python YASB makes); not exercised live ([release checklist](release-checklist.md)), so the default output is never changed by tests | `CoCreateInstance` or the call failing logs once and returns false; the source menu keeps the old default |
| `IQuietHoursSettings` ({6BFF4732-81EC-4FFB-AE67-B6C1BC29631F}) from `CLSID_QuietHoursSettings` ({F53321FA-34F8-4B7F-B9A3-361877CB94CF}), `get_UserSelectedProfile` / `put_UserSelectedProfile` directly after `IUnknown`, profile ids `Microsoft.QuietHoursProfile.*` | `src/features/notifications/win32/dnd_win.cpp` | Windows 11 build 26300, ARM64 (read verified live: `disabled`); the write path is not exercised live ([release checklist](release-checklist.md)) | creation or read failure reports `unknown`, which the Do Not Disturb tile shows as active (as Python does); a failed write logs and the tile state is unchanged |
| `ntdll!RtlSubscribeWnfStateChangeNotification` (through `WnfSubscription`, `win32/wnf_win.cpp`) with WNF state name `WNF_SHEL_QUIETHOURS_ACTIVE_PROFILE_CHANGED` (0x0D83063EA3BF1C75), the callback ignores the buffer and re-reads the profile through `IQuietHoursSettings` | `src/features/notifications/win32/dnd_win.cpp` (`DndWatcher`) | name from Python YASB; not exercised live ([release checklist](release-checklist.md)) on 26300 | a missing symbol or a non-zero status logs a warning and the Do Not Disturb button then updates only on its own clicks |
| `user32!SetWindowCompositionAttribute` with `WCA_ACCENT_POLICY` (19), local `AccentPolicy` / `CompositionAttribData` structs | `src/host/win32/backdrop.cpp` (`setAccentPolicy`) | Windows 11 build 26300, ARM64 | resolved with `GetProcAddress`; missing symbol or failure throws inside `enableBlur`, which catches and logs; bar keeps its plain background |
| `ntdll!RtlGetVersion` (documented for drivers; used because `GetVersionEx` lies to unmanifested apps) | `src/kernel/win32/windows_build.cpp` (`windowsBuild`; users: `src/host/win32/backdrop.cpp`, `src/features/system_stats/win32/pdh.cpp` CPU counter choice, `src/features/control_center/win32/power_win.cpp` `isWindows10`) | Windows 11 build 26300, ARM64 | build 0: backdrop takes the pre-Windows 11 paths, PDH uses `% Processor Utility` and falls back to `% Processor Time` |
| Numeric DWM attributes: `DWMWA_USE_IMMERSIVE_DARK_MODE` (20), `DWMWA_WINDOW_CORNER_PREFERENCE` (33), `DWMWA_BORDER_COLOR` (34), `DWMWA_SYSTEMBACKDROP_TYPE` (38), undocumented Mica attribute 1029, colour sentinels `0xFFFFFFFF`/`0xFFFFFFFE` | `src/host/win32/backdrop.cpp` | Windows 11 build 26300, ARM64 | gated on `windowsBuild()` (22000 / 22621); `DwmSetWindowAttribute` failures are caught and logged |

## History

waku began as a C++ port of Python YASB. That port, built on Qt Widgets with YAML and CSS config,
was retired. waku replaced it with a Qt Quick shell runtime
configured in QML, and in October 2026 the code was reorganised by capability
into the units above (`docs/plans/2026-10-29-waku-architecture-proposal.md`). Python YASB remains a
reference for how Windows behaves, not for how waku must behave.
