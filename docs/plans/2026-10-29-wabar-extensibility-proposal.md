# wabar extensibility: user-defined surfaces on every edge, launchers and overlays

Status: proposal, 2026-10-29. Nothing here is implemented. Branch `restructure/wabar` at `34a88e5`.
Companion documents: `2026-10-29-wabar-architecture-proposal.md` (the units and rules S1-S8) and
`2026-10-29-wabar-styling-proposal.md` (tokens, style bundles, the shadowing finding).
Revised 2026-10-30: section 11 adopts Quickshell's exact vocabulary and adds what porting
caelestia needs. Section 11 wins where it conflicts.

The owner's goal: users write their whole shell in QML. Not only a top bar: bars on any of the
four edges, a launcher, a dock, an OSD, notification popups, menus, a clipboard history, widget
panels. The engine holds only what QML cannot do: windows, Win32 integration, data sources and
services. The launcher is the next use case and must need no engine change once the model exists.

Qt behaviour cited below was checked against the Qt 6 documentation (Context7, `doc.qt.io/qt-6`) on
2026-10-29; the points checked are listed in section 10. Statements about QuickShell come from my
own knowledge of its documentation, not from a source read today, and are marked "(QuickShell, from
memory)".

## 1. Summary and recommendation

The engine already has the right shape: a config is a declaration, the host builds one window per
matching screen from it, validates a new config in a throwaway engine before tearing the old one
down, and reaches Windows through replaceable hooks (`src/host/qml/bar_host.h:44-52`). What blocks
the goal is that this machinery knows exactly one window kind. `Bar` is a top-or-bottom strip with
a `height` (`src/host/qml/bar_config.h:35,23`), the app bar code knows two edges
(`src/host/app_bar.h:11`), `BarWindow.qml` is "never written by users" (`src/host/qml/BarWindow.qml:4`)
and is the only window the host can build (`src/host/qml/bar_host.cpp:22,54`), and popups,
tooltips, islands and `Pill` all read `Window.window.spec.position` as Top or Bottom
(`src/host/qml/PopupWindow.qml:51`, `Tooltip.qml:41`, `BarLayout.qml:71-72`, `src/ui/Pill.qml:40`).
The wallpaper gallery already is a second surface kind, written by hand as a full-screen `Window`
with its own screen pick, activation and dismissal (`src/features/wallpapers/qml/WallpaperGallery.qml:14-83`)
and created from C++ by URL (`src/features/wallpapers/qml/wallpaper_settings.cpp:74-92`). That
is the pattern that must not repeat.

Recommendation, in one sentence: replace `Config { Bar {} }` with `Shell { Panel {} Overlay {} }`,
where `Surface` is the engine's one window primitive (screens, edge or anchors, size, layer, focus
mode, input region, backdrop, exclusive zone, show/hide/toggle, dismissal, per-screen
instantiation), `Panel` and `Overlay` are its two creatable kinds, `PopupWindow` and `Tooltip` keep
anchoring to items, and everything a surface shows is a user `Component`. Around it: a `Hotkey` type,
an `IpcHandler` type with `wabarc surface|call` verbs, a `Desktop` events singleton, a `Persist`
object that survives hot reload, and three new data features (`apps`, `desktop_windows`,
`clipboard`) plus a fuller `Process`. Styling follows the styling proposal (O2 + O3), with the four
building blocks (`Pill`, `Island`, `PopupFrame`, `TooltipFrame`) made public and `wabarc eject`
replacing the broken shadowing story. Built-ins stay compiled in and are also shipped as loose
reference files; release never loads the loose copy.

Clean break: the owner is the only user, so `Config`, `Bar` and `BarLayout` are deleted, not
aliased, and the `wabar` module becomes version 2.0. Migration of a `bar.qml` is a rename and six
property renames (section 4.2.4).

The biggest risk is Windows' foreground-activation rule: a launcher shown from a hotkey or from
`wabarc` must take keyboard focus away from the foreground app, and Windows refuses that to a
process that did not receive the last input. The repository has one workaround already
(`src/features/wallpapers/win32/window_focus.h:11`, `AttachThreadInput`), and `wabarc` can grant
permission with `AllowSetForegroundWindow` before it sends a command. Both need the spike in
section 10 before step 4 of the migration is sized. The second risk is the vertical bar: it is not a
separate design, but it touches `Pill`, the three `Row`-rooted widgets, the island canvas and
popup placement (section 4.1.6).

## 2. Goals and non-goals

Goals:

- A user writes bars on any edge, a launcher, a dock, an OSD, popups and menus in QML, with no
  engine change for a new surface of an existing kind.
- The engine exposes windows, Win32 integration, data sources and services; nothing else.
- Every surface hot-reloads, validates before it replaces the live one, fails soft, and is
  controllable from `wabarc` and from hotkeys.
- Headless tests for everything the engine adds; live tests only where the live-desktop rules
  allow, and never against the user's real bar, wallpaper, tray or Explorer.
- The structure rules S1-S8 keep holding, with named changes where they must bend.

Non-goals:

- Parity with Wayland layer-shell, and with Quickshell as a whole (revised: source compatibility
  with Quickshell QML for the subset with a Windows meaning is a goal, section 11). Windows has no
  layer-shell; section 5 says what cannot be had and what the fallback is.
- A sandbox for user QML. The config is code and runs as the user (section 8).
- A GUI editor, a package manager for shells, or a plugin ABI for C++ extensions.
- Porting every built-in widget to a vertical variant in one go. The migration gives the vertical
  bar a point where the layout and the common building block work; widgets follow one by one.

## 3. Current state

### 3.1 What already supports the goal

| What | Where | Why it helps |
|---|---|---|
| Config as a declaration; the host builds windows from it | `src/host/qml/bar_config.h:14-16`, `src/host/qml/bar_host.cpp:273-295` | A surface family is the same idea with more kinds |
| Per-screen instantiation with `screens: ["primary" \| "all" \| name]` | `src/host/screen_match.h:13-19`, `bar_host.cpp:275` | This is QuickShell's `Variants` for the screen case, already pure and tested |
| Validation in a throwaway engine before the live config is replaced | `bar_host.cpp:117-145,202-239` | Any new surface kind inherits transactional reload |
| Non-visual "parts" next to bars, activated after install | `bar_config.h:66-72`, `bar_host.cpp:305-316` | The pattern `Hotkey`, `IpcHandler` and `Persist` need |
| Native hooks behind `std::function`, faked in tests | `bar_host.h:44-52`, `src/host/testing/bar_fixtures.h:94-122` | New Win32 primitives slot in without touching the tests' safety |
| App bar registration with `WS_EX_NOACTIVATE` and `WS_EX_TOPMOST` | `src/host/win32/app_bar.cpp:29-48` | The docked-panel primitive exists for two edges; `AppBarEdge` values already equal `ABE_TOP` (1) and `ABE_BOTTOM` (3) (`app_bar.h:11`) |
| Blur, Mica, DWM frame | `src/host/win32/backdrop.cpp:90-143` | Backdrops exist; Acrylic is one constant away (`backdrop.cpp:28,107`) |
| Popup and tooltip windows with pure placement | `src/host/qml/PopupWindow.qml`, `Tooltip.qml`, `src/host/popup_placement.h:17-18` | Anchored surfaces exist; placement needs Left and Right |
| A full-screen overlay surface, written by hand | `WallpaperGallery.qml:55-83` | Proves topmost tool windows, `requestActivate`, dismissal on `activeChanged` and Escape work on this machine |
| Global hotkey through `RegisterHotKey` and a native event filter | `src/features/wallpapers/win32/wallpaper_hotkey_win.cpp:21-51`, interface `wallpaper_hotkey.h:20-26` | The pattern for a generic `Hotkey`; today one id (`kId`, line 54) and one key |
| Control pipe, one line in, one out, user-only, instance mutex | `src/control/control.h:11-16`, `control.cpp:85,97` | The transport for per-surface IPC exists; only the verb table is fixed (`src/host/qml/control_handler.cpp:50-73`) |
| `Process` and `Shell` | `src/features/shell/qml/child_process.h:19-31`, `shell_service.h:39-46` | Users already script without C++ (`examples/config/widgets/Whoami.qml`) |
| Window tracking, identity, icons, activation | `src/features/taskbar/window_system.h:53-88`, `qml/taskbar_service.h:48-146` | A window-list service needs a QML face, not new Win32 |
| AUMID lookup and activation of Store apps | `src/kernel/win32/app_activation.h` (per `docs/architecture.md:97`) | Half of launching a Store app from a launcher |
| Shell item icons | `window_system.h:49` (`shellItemIcon`) | App icons for a launcher |
| Image providers, including an asynchronous one | `src/features/wallpapers/qml/wallpapers_service.h:166-176`, `taskbar_service.h:148-157`, `src/features/tray/qml/tray_image_provider.h` | The `image://appicon` provider follows one of these |
| Foreground forcing and foreground monitor | `src/features/wallpapers/win32/window_focus.h:11,14` | Needed by overlays that take focus; a kernel candidate (two users) |
| The live-desktop gate | `src/kernel/qt/live_desktop.h:19-28` | Every new service keeps the test safety rule with one call |
| Safe mode and fallback | `src/app/wabar/SafeConfig.qml`, `src/host/qml/FallbackConfig.qml`, `bar_host.cpp:177-200` | The recovery path survives the surface family unchanged |
| Hot reload of the whole config home | `bar_host.cpp:354-364` | New folders (`surfaces/`) are watched for free |

### 3.2 What blocks it

| Block | Evidence |
|---|---|
| One window kind, host-owned, loaded once by fixed URL | `bar_host.cpp:22,54-59`; `BarWindow.qml:4` |
| `Bar` knows Top and Bottom and a `height` | `bar_config.h:23,35`; `BarWindow.qml:17-20` |
| App bar: two edges, rect for top and bottom only, the adjusted rect from `ABM_SETPOS` is never read back | `app_bar.h:11`, `app_bar.cpp:8-23,47` |
| Backdrop enum lacks Acrylic | `bar_config.h:37` |
| Popups and tooltips open Below or Above only | `popup_placement.h:10`; `PopupWindow.qml:51`; `Tooltip.qml:41` |
| `Pill.flat` means "the window has a `spec`" | `Pill.qml:40` |
| `BarLayout` is three `Row`s; the island canvas is horizontal and only flips for Bottom | `BarLayout.qml:169-189,71-72,109-155` |
| Three widgets are rooted in a `Row` (`Workspaces`, `Taskbar`, `Tray`); thirteen are rooted in `Pill`, which lays icon and text in a `Row` | `Workspaces.qml:14`, `Taskbar.qml:21`, `Tray.qml:19`, `Pill.qml:68-95` |
| One failed window creation fails the whole build | `bar_host.cpp:281-287` |
| One hotkey, owned by one feature | `wallpaper_hotkey_win.cpp:54`; `src/control/` holds no hotkey code (`control.h`, `wallpaper_control.h`, `win32/instance_mutex.*`) |
| `wabarc` validates verbs on the client, so a new server verb needs a client change | `src/app/wabarc/main.cpp:79-112` |
| `Process` returns the whole output on `finished`; no streaming, stdin, environment, working directory or kill | `child_process.h:19-31` |
| The window list reaches QML only through `TaskbarModel`, and the tracker needs a bar window to start | `Taskbar.qml:62-73`; `taskbar_service.h:82-84` |
| No app catalogue, no clipboard listener, no desktop events (screens, fullscreen, session lock) | nothing under `src/` matches; `QGuiApplication::screenAdded` is not connected anywhere |
| Nothing survives a reload except C++ singletons; a typed launcher query would be lost | `bar_host.cpp:214-217` (tear down, clear cache, rebuild) |
| Shadowing a built-in by a same-named `widgets/` file does not work (directory imports lose to modules) | styling proposal section 2.2; `docs/architecture.md:144-147` still says "relies on import order" |

## 4. Design

### 4.1 Surface model

#### 4.1.1 Types

All in the `wabar` module (`src/host/qml/`). Names are proposals.

| Type | Creatable | What |
|---|---|---|
| `Shell` | yes, root of `shell.qml` | Default property takes surfaces and parts (as `Config` does today, `bar_config.h:73-119`) |
| `Surface` | no (`QML_UNCREATABLE`) | The window primitive: everything below that is common |
| `Panel` | yes | A surface docked to a screen edge. Replaces `Bar` |
| `Overlay` | yes | A surface shown on demand: floating, anchored to edges, or full screen. Launcher, OSD, power menu, notification stack, dock without exclusive zone |
| `PopupWindow` | yes | Unchanged role: a window anchored to an item inside another surface |
| `Tooltip` | yes | Unchanged role |
| `PanelLayout` | yes | Replaces `BarLayout`: start, centre, end groups along the panel's axis |
| `Island` | yes | The island background, extracted from `BarLayout.qml:65-158`, usable by any surface |
| `PopupFrame`, `TooltipFrame` | yes | The chrome of popups and tooltips as items, so an `Overlay` can look like a popup |
| `Hotkey`, `IpcHandler`, `Persist` | yes | Parts (section 4.3, 4.6) |
| `Desktop` | singleton | Screens, foreground, session, display events (section 4.3) |
| `Theme`, `ThemeSpec`, `PopupPlacement` | as today | |
| `PanelWindow.qml`, `OverlayWindow.qml`, `SafeShell.qml`, `FallbackShell.qml` | no | Host-owned windows and the recovery shells; marked internal (section 6) |

`Surface` properties, with the Win32 mechanism each one maps to in section 5:

```
name            string      the id for wabarc, hotkeys and Persist; unique per shell
screens         list        "primary", "all" or screen names (as today)
enabled         bool
layer           enum        Surface.Bottom | Surface.Normal | Surface.Top (default for Panel) |
                            Surface.Overlay (default for Overlay: topmost and raised on show)
focus           enum        Surface.NoFocus (default for Panel) | Surface.OnDemand (default for
                            Overlay: activated on show, releases focus on hide)
input           enum        Surface.Accept (default) | Surface.PassThrough (click-through)
backdrop        enum        Surface.None | Blur | Acrylic | Mica
background      color       unset: Theme.surface (as today)
dismissOnBlur   bool        hide when the window loses activation (focus: OnDemand only)
dismissOnEscape bool
hideDelay       int ms      hide() hides the window after this delay so content can animate out
visible         bool        read: shown; Overlay starts hidden, Panel starts shown
content         Component   built once per matching screen (today's `layout`)
show(), hide(), toggle()    invokables; signals shown(), hidden()
```

`Panel` adds `edge` (`Panel.Top | Bottom | Left | Right`), `thickness` (today's `height`),
`length` (0 = the whole edge; otherwise logical pixels, with `alignment` Start | Center | End),
`margins` (a rect of four ints; a dock-like floating panel is a panel with margins and `exclusive:
false`), `exclusive` (today's `reserveSpace`, still gated by `--register-app-bar`), and
`hideOnFullscreen` (section 4.3).

`Overlay` adds `anchors` (a set of `Overlay.Top | Bottom | Left | Right`; none means centred on
the screen; two opposite edges stretch), `margins`, `width` and `height` (0 = the content's
implicit size), and `fullscreen` (covers the screen; the gallery's case).

#### 4.1.2 What the content sees

Every surface window exposes `Window.window.surface` (today: `spec`, `BarWindow.qml:8`),
`Window.window.monitorHandle` (unchanged, `BarWindow.qml:13`) and `Window.window.host`
(unchanged). `surface` is the declaration plus three derived read-only properties the host
computes per window: `kind` (`Surface.Panel | Overlay | Popup`), `edge` for a panel, and
`vertical` (`edge` is Left or Right). `Pill.flat` becomes
`Window.window.surface !== undefined && Window.window.surface.kind === Surface.Panel`, and
`Workspaces.qml:27` keeps reading `monitorHandle` unchanged.

#### 4.1.3 The host

`BarHost` becomes `ShellHost` (`src/host/qml/shell_host.*`). Changes to `build()`
(`bar_host.cpp:241-297`):

- It iterates `Shell::surfaceList()` and picks the window component per kind (`PanelWindow.qml`,
  `OverlayWindow.qml`), both loaded once as `windowComponent_` is today.
- Each `Entry` gets a `SurfaceController` (a `QObject` the window binds to): `show()`, `hide()`,
  `toggle()`, `visible`, the derived properties of 4.1.2, and the app bar handle. The host keeps a
  map `name -> controllers` for IPC and hotkeys; one name has one controller per screen.
- A surface whose window fails to build is skipped with its error recorded; the other surfaces
  still install. The fallback shell is used only when no surface at all could be built. Today the
  first failure aborts everything (`bar_host.cpp:281-287`).
- `present()` (`bar_host.cpp:319-339`) applies `layer`, `focus`, `input`, `backdrop`, and the app
  bar for an exclusive panel with the edge's rect. For an overlay it applies the same but does not
  show the window.
- `NativeHooks` grows: `makeAppBar` (four edges), `applyBackdrop` (four kinds), `setNoActivate`,
  `setLayer`, `activate` (foreground forcing), `monitorHandle` (exists). Fakes record calls, as the
  `Native` struct does (`bar_fixtures.h:94-122`).

`Shell` parts (`Hotkey`, `IpcHandler`, `Persist`, `WallpaperSettings`) are activated after install
exactly as today (`bar_host.cpp:305-316`), by the `activate()` convention. A part that needs the
host (a `Hotkey` needs the controller map) finds it through `Window.window.host`'s sibling: the host
sets a context property `ShellHost` on the engine before the config is built, the way
`qmlEngine(this)->singletonInstance` is used in `wallpaper_settings.cpp:44`.

#### 4.1.4 Popups and tooltips on a panel of any edge

`PopupPlacement` gains `Left` and `Right` edges (`popup_placement.h:10`), with the anchor rect
being the item's rect extended to the panel's thickness along the panel's axis. `PopupWindow.qml:46-58`
and `Tooltip.qml:38-48` take the edge from `surface.edge`: Top opens Below, Bottom opens Above,
Left opens Right, Right opens Left. Inside an `Overlay` a popup opens Below by default and
`preferredEdge` overrides. Dismissal keeps its two paths: activation loss (`PopupWindow.qml:94-99`)
and the host's press report (`bar_host.cpp:72-85`), which now covers every surface window.

Qt note checked: a `Window` declared inside an `Item` gets that item's window as transient parent
and is not shown until the parent is shown. A popup inside a hidden overlay therefore cannot open,
which is the wanted behaviour; a popup must never set `transientParent: null`.

#### 4.1.5 Per-screen instantiation

`screens` stays the engine's `Variants` (QuickShell, from memory: `Variants { model:
Quickshell.screens; delegate: PanelWindow {} }`). It is simpler and already tested
(`src/host/tests/test_screen_match.cpp`). A shell that wants a different panel per screen declares
two panels with different `screens` lists. The host rebuilds all surfaces when the screen set
changes (section 4.3), which also covers a `Panel` whose screen was unplugged.

#### 4.1.6 Vertical panels: not a separate design, but every layer needs one change

Verified against the Qt docs: `Column`, `ColumnLayout` and `ListView { orientation:
Qt.Vertical }` exist, and `Flow { flow: Flow.TopToBottom }` lays items along one axis without
wrapping while it has room. There is no engine obstacle.

| Layer | Today | Change |
|---|---|---|
| Panel window | `x/y/width/height` for Top and Bottom (`BarWindow.qml:17-20`) | `PanelWindow.qml` computes geometry from `edge`, `thickness`, `length`, `alignment`, `margins` |
| App bar rect | top and bottom (`app_bar.cpp:8-23`) | left and right rects; physical pixels on every axis (section 5, DPI row) |
| `PanelLayout` | three `Row`s (`BarLayout.qml:169-189`) | a `Strip` positioner per group: `Flow` with `flow` bound to `surface.vertical`; start anchors top for a vertical panel, end anchors bottom |
| `Island` | horizontal path, flipped for Bottom (`BarLayout.qml:109-155`) | draw in a local frame and transpose for Left and Right (the fillets meet the screen edge on the panel's own edge) |
| `Pill` | icon and text in a `Row` (`Pill.qml:68-95`); `implicitHeight: Theme.pillHeight` | `vertical` (bound to `surface.vertical`): icon above text, `implicitWidth: Theme.pillHeight`, text kept horizontal and elided to the panel's thickness; `rotateText: true` as an opt-in for long labels |
| `Row`-rooted widgets: `Workspaces`, `Taskbar`, `Tray` | `Row` | root becomes `Strip` (`src/ui/Strip.qml`) |
| Popups and tooltips | Below or Above | section 4.1.4 |
| Everything else (`Clock`, `Cpu`, `Memory`, `Battery`, `Volume`, `Media`, `Wifi`, `Bluetooth`, `Notifications`, `GitHub`, `ControlCenter`, `Wallpapers`, `ActiveLayout`) | `Pill` roots (13 files) | inherit the `Pill` change; `Media` and `Taskbar` have extra horizontal assumptions (album art beside text, button widths) and are sized separately in the migration |

Text in a vertical panel: horizontal by default, because at 13 px a 48 px wide panel fits a clock
"14:32" and a workspace label but not a date; the user picks the format, as on a top bar. Rotated
text is an opt-in on `Pill`, not a theme default.

### 4.2 Public QML API

#### 4.2.1 Modules, versions, deprecation

| Module | Version | Public types |
|---|---|---|
| `wabar` | 2.0 | `Shell`, `Surface` (enums only), `Panel`, `Overlay`, `PopupWindow`, `Tooltip`, `PanelLayout`, `Island`, `PopupFrame`, `TooltipFrame`, `Hotkey`, `IpcHandler`, `Persist`, `Desktop`, `Theme`, `ThemeSpec`, `PopupPlacement` |
| `wabar.widgets` | 2.0 | every built-in widget; `Pill`, `Strip`, `Slider`, `DeviceMenu`, `DeviceRow` |
| `wabar.services` | 2.0 | every existing service; `Apps`, `Windows`, `Clipboard`, the fuller `Process` |

Private (not for user QML): `ShellHost`, `SurfaceController`, `PanelWindow`, `OverlayWindow`,
`SafeShell`, `FallbackShell`, the `wabar.services` model types that widgets instantiate (`TaskbarModel`
stays as it is, used by `Taskbar.qml:62`). Qt's `qt_add_qml_module` lists every QML file in the
`qmldir`; marking a file internal (`QT_QML_INTERNAL_TYPE` on the source file) is the mechanism to
verify in step 1 (section 10).

Versioning: the URIs never change (`docs/architecture.md:118`). The version is semver on the
module: adding a type or property is a minor bump; removing or renaming is a major bump. Before
1.0 of the product the rule is relaxed to "break, with a mapping table in the plan and in
`docs/building.md`", which is what this proposal does: `wabar` 1.0 -> 2.0 with `Config`, `Bar` and
`BarLayout` gone. After the product's 1.0: a removed type stays for one minor release as a
`QML_UNCREATABLE` stub whose creation error names the replacement, and the host logs one line per
reload for each deprecated type it finds in the validated config. `wabarc check` (section 4.3)
prints the same list, so deprecations are visible without reading the log.

#### 4.2.2 What is borrowed from QuickShell and what Windows changes

(QuickShell, from memory.)

| QuickShell | wabar | What Windows changes |
|---|---|---|
| `PanelWindow` with `anchors`, `exclusiveZone`, `exclusionMode`, `margins`, `aboveWindows`, `focusable`, `mask` | `Panel` and `Overlay` on `Surface` with `edge`/`anchors`, `exclusive`, `margins`, `layer`, `focus`, `input` | Exclusive zones exist only for the four screen edges (`SHAppBarMessage`); no arbitrary exclusive rects. No per-window "namespace" layers; three z-order classes instead |
| `FloatingWindow` | `Overlay` with no anchors, or a normal decorated `Window` from QtQuick (no engine type needed) | nothing |
| `PopupWindow` with `anchor.item`, `anchor.rect`, `anchor.window` | `PopupWindow` with `anchorItem`; `anchorRect` added | nothing |
| `Variants` | `screens` on every surface | nothing |
| `Scope`, `ShellRoot` | `Shell` with a default property that takes non-visual parts | nothing |
| `Process` with `command`, `running`, `stdout` parsers, `stdin`, `environment`, `exited` | `Process` extended (section 4.4) | hidden console window (`CREATE_NO_WINDOW`) is the engine's job (`src/kernel/win32/shell_launch.h:15`) |
| `DesktopEntries` (`.desktop` files) | `Apps` (Start menu shortcuts and `shell:AppsFolder`) | two sources, two launch paths, no `.desktop` format |
| `IpcHandler` with `qs ipc call target fn args` | `IpcHandler` with `wabarc call target fn args` | one line in, one out, user-only pipe |
| `GlobalShortcut` (compositor-provided) | `Hotkey` (`RegisterHotKey`) | global, but first come first served; `Win+key` combinations are reserved by the shell; a taken combination fails soft and `registered` is false |
| `PersistentProperties` | `Persist` | nothing |
| `LazyLoader` | QML `Loader { active: ... }` | nothing |
| `Quickshell.screens`, `Quickshell.env` | `Desktop.screens`, `Shell.env` (exists, `shell_service.h:46`) | nothing |
| Keyboard focus modes `exclusive`, `on-demand` (layer-shell) | `focus: OnDemand` only | no exclusive keyboard grab on Windows without a low-level keyboard hook; rejected (section 5) |

#### 4.2.3 Examples against the proposed API

A top bar, the current config translated (`examples/config/bar.qml:7-33`):

```qml
import wabar
import wabar.widgets
import "widgets"

Shell {
    Panel {
        name: "top"
        edge: Panel.Top
        thickness: 40
        backdrop: Surface.Blur
        content: PanelLayout {
            start:  [ Clock {} ]
            center: [ Workspaces {}, BindingMode {} ]
            end:    [ Whoami {}, Media {}, Volume {}, Battery {}, Cpu {}, Memory {} ]
        }
    }
}
```

A left vertical bar on every screen, exclusive, with popups opening to the right:

```qml
Shell {
    Panel {
        name: "side"
        screens: ["all"]
        edge: Panel.Left
        thickness: 48
        exclusive: true                 // takes effect with --register-app-bar
        backdrop: Surface.Mica
        content: PanelLayout {
            start:  [ Workspaces { style: "dots" } ]
            center: [ Clock { format: "HH\nmm" } ]
            end:    [ Volume {}, Battery {}, Tray { unpinnedInPopup: true } ]
        }
    }
}
```

A bottom dock: a short non-exclusive panel, hidden under a full-screen app:

```qml
import wabar.services as S

Shell {
    Panel {
        name: "dock"
        edge: Panel.Bottom
        thickness: 56
        length: 0                       // 0: as long as its content
        alignment: Panel.Center
        margins.bottom: 8
        exclusive: false
        hideOnFullscreen: true
        backdrop: Surface.Acrylic
        content: Island {
            Strip {
                Repeater {
                    model: S.Windows.list
                    delegate: Pill {
                        required property var modelData
                        icon: ""
                        Image { source: modelData.icon; width: 32; height: 32 }
                        onClicked: S.Windows.activate(modelData.hwnd)
                    }
                }
            }
        }
    }
}
```

A launcher, the next use case: an overlay toggled by a hotkey and by `wabarc`, a typed query that
survives a reload, apps ranked by the engine, a calculator provider in plain JS:

```qml
import QtQuick
import QtQuick.Controls
import wabar
import wabar.widgets
import wabar.services as S

Shell {
    Overlay {
        id: launcher
        name: "launcher"
        width: 640
        height: 420
        focus: Surface.OnDemand
        dismissOnBlur: true
        dismissOnEscape: true
        hideDelay: Theme.popupFadeMs
        backdrop: Surface.Acrylic

        Persist { id: state; property string query: "" }

        content: PopupFrame {
            opacity: launcher.visible ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.popupFadeMs } }

            Column {
                anchors.fill: parent
                TextField {
                    id: input
                    text: state.query
                    onTextChanged: state.query = text
                    focus: true
                    Keys.onReturnPressed: list.currentItem?.run()
                }
                ListView {
                    id: list
                    model: results(input.text)
                    delegate: Pill {
                        required property var modelData
                        text: modelData.name
                        function run() { modelData.run(); launcher.hide() }
                        onClicked: run()
                    }
                }
            }

            function results(q) {
                if (/^[\d\s+\-*/().]+$/.test(q))           // calculator: pure JS, no engine
                    return [{ name: q + " = " + Function("return " + q)(), run: () => {} }];
                return S.Apps.search(q, 12).map(a => ({ name: a.name, run: () => S.Apps.launch(a.id) }));
            }
        }
    }

    Hotkey { keys: "alt+space"; onPressed: launcher.toggle() }
    IpcHandler {
        target: "launcher"
        function open(query: string): string { state.query = query; launcher.show(); return "ok" }
    }
}
```

`wabarc surface toggle launcher` works with no `IpcHandler` at all; the handler adds
`wabarc call launcher open "term"`.

#### 4.2.4 Migration of an existing `bar.qml`

| Old | New |
|---|---|
| file `bar.qml` | file `shell.qml`; a config home with `bar.qml` and no `shell.qml` gets the error "rename bar.qml to shell.qml and Config to Shell" in the fallback shell |
| `Config` | `Shell` |
| `Bar` | `Panel` |
| `position: Bar.Top` | `edge: Panel.Top` |
| `height` | `thickness` |
| `reserveSpace` | `exclusive` |
| `backdrop: Bar.Blur` | `backdrop: Surface.Blur` |
| `layout: BarLayout {}` | `content: PanelLayout {}` |
| `BarLayout.startFill`, `centerFill`, `endFill` | style bundles `island.start` and friends (styling proposal 4.2) |
| `Window.window.spec` | `Window.window.surface` |

`WallpaperSettings` and user widgets are unchanged. No automatic migration tool: one user, ten
lines.

### 4.3 Triggers and control

**Hotkeys.** `Hotkey { keys: "alt+space"; onPressed }` in `wabar`, a part. Interface
`src/host/hotkeys.h` (`HotkeyHost::registerKey(combo, id) -> bool`, several ids), Win32
implementation `src/host/win32/hotkeys_win.cpp` (the `RegisterHotKey`/native-filter code of
`wallpaper_hotkey_win.cpp:17-57` generalised to many ids), fake in `src/host/testing/`.
`parseHotkey` (`wallpaper_hotkey.h:16`) moves to the host. `registered` is a read-only property;
a taken combination logs once and leaves it false. Hotkeys are registered on install and
unregistered on teardown, so a reload re-registers; a hotkey pressed during the 150 ms debounce is
lost, which is acceptable. `WallpaperSettings.hotkey` stays as a convenience and uses the host
interface, so the two never fight over an id. Inert when `kernel::liveDesktop("hotkeys")` is false.

**IPC.** The control handler (`control_handler.cpp:43-75`) gains a registry the host fills:

```
wabarc surface list
wabarc surface show|hide|toggle|status <name>
wabarc call <target> <function> [args...]      IpcHandler; args are strings, converted by the
                                                 declared parameter types; the return value is one
                                                 line (JSON when it is an object or a list)
wabarc check [<dir>]                             validate a config home without applying it
wabarc eject <Widget> [<NewName>]                section 4.5
wabarc reload | status | quit | wallpaper ...    as today
```

`wabarc` stops validating verbs itself (`src/app/wabarc/main.cpp:79-112`) and forwards anything it
does not know; the bar's answer decides the exit code, as it does today (`main.cpp:130-132`).
Before sending `surface show|toggle` or `call`, `wabarc` calls `AllowSetForegroundWindow(pid of
the bar)` (documented), which grants the bar the right to take the foreground once; the bar's pid
is answered by `status` or carried in the pipe handshake. Section 5 says why.

**Events.** `Desktop` singleton in `wabar` (`src/host/qml/desktop.h`, Win32 in
`src/host/win32/desktop_events_win.cpp`):

- `screens` and `screensChanged`: from `QGuiApplication::screenAdded`, `screenRemoved` and
  `primaryScreenChanged` (verified in the Qt docs). The host rebuilds all surfaces on a change,
  debounced, through the same path as a reload; simplest and correct for app bars.
- `foregroundFullscreen` (bool) and `foregroundWindow`: `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)`
  (documented) plus a rect-equals-monitor check. `Panel.hideOnFullscreen` binds to it. The taskbar
  feature has the same hook (`src/features/taskbar/win32/window_tracker_hooks_win.cpp`); the host
  cannot include a feature (S2), so the host gets its own thirty lines, not a shared one, unless
  step 10 moves the tracker to `desktop_windows` and the host still cannot use it. Accepted
  duplication; named in `docs/architecture.md`.
- `sessionLocked`: `WTSRegisterSessionNotification` and `WM_WTSSESSION_CHANGE` (documented) on a
  hidden message window. Overlays hide on lock.
- `workAreaChanged`: `WM_SETTINGCHANGE` with `SPI_SETWORKAREA`; `QScreen::availableGeometryChanged`
  carries most of it.
- `darkMode`: the control-centre feature reads the app theme today; the host gets its own registry
  read (documented `HKCU\...\Themes\Personalize\AppsUseLightTheme`) so `theme.qml` can bind to it
  (section 4.5, question 5).

All of it inert on the offscreen platform; the tests drive the singleton through a fake.

**Pipe security.** Unchanged: `QLocalServer::UserAccessOption` (`control.cpp:85`) keeps other users
out; the single-instance mutex (`control.cpp:97`) ties one server to one config home; 1 KiB per
line, 3 s idle drop (`control.h:40-41`). `call` lets the same user run a QML function that the
same user wrote, with the same rights the user's config already has. Nothing widens beyond the
local user; `SECURITY.md` says so already (`SECURITY.md:5-9`) and gets the new verbs listed.

### 4.4 Data services

Rule for the split: a thing goes into the engine when QML cannot observe or do it (a Win32
notification, COM, a process), and into example QML when it is a computation over data the engine
already exposes.

| Need | Where | What |
|---|---|---|
| App catalogue | engine: new feature `apps` | `S.Apps.entries` (list of `{id, name, kind: "shortcut" \| "store", path, aumid, icon}`), `search(query, limit)` ranked, `launch(id)`, `launchAsAdmin(id)` (`ShellExecuteEx` verb `runas`; Store apps have no admin path and return false), `openLocation(id)`, `refresh()`. Sources: `.lnk` files under the user and common Start Menu folders (`FOLDERID_Programs`, `FOLDERID_CommonPrograms`, resolved with `IShellLink`, all documented) and `shell:AppsFolder` (`SHGetKnownFolderItem(FOLDERID_AppsFolder)` and `IEnumShellItems`, documented; gives Store apps with their AUMID). Launch: `ShellExecuteEx` on the shortcut; `activateAppByAumid` for Store apps (`src/kernel/win32/app_activation.h`). Icons: `image://appicon/<id>` through an asynchronous provider that calls `shellItemIcon` (`window_system.h:49`), which moves to `src/kernel/win32/shell_icons.*` (users: taskbar, apps). Ranking is a pure function in `src/features/apps/rank.h` (prefix, word start, subsequence; frecency as a tie breaker) with tests. Frecency lives in `%LOCALAPPDATA%\wabar\apps_frecency.json` (`appDataDir()`). The scan runs on the global `QThreadPool` and posts results; a `QFileSystemWatcher` on the Start Menu folders triggers a rescan. `Deps` takes the folder list, so tests scan a temp folder with fake `.lnk` files and never the machine's; the real constructor reads the real folders only when `kernel::liveDesktop("apps")`. Fake: `testing/fake_app_catalog.h` |
| Run and script | engine: `shell` feature, `Process` extended | `environment` (map), `clearEnvironment`, `workingDirectory`, `stdin` with `write(text)` and `closeInput()`, `stream: true` emitting `line(text)` per stdout line (`readyReadStandardOutput`, verified), `json` (parsed `output`, null when not JSON), `kill()`, `terminate()`, `exited(code)`. `QProcess` supports all of it (verified: `setProcessEnvironment`, `kill`, `terminate`, `readyReadStandardOutput`). `Shell.start/command/open` unchanged |
| Window list | engine: new feature `desktop_windows` extracted from `taskbar` (the architecture proposal named it, section 3 there) | `S.Windows.list` (`{hwnd, title, process, icon, active, minimized, monitorHandle}`), `active`, `activate(hwnd)`, `close(hwnd)`, `minimize(hwnd)`. `WindowSystem`, `WindowTracker`, app identity and their fakes move; `taskbar` declares `DEPENDS desktop_windows` and keeps pins, grouping and `TaskbarModel`. The tracker today needs a top-level window from a widget (`taskbar_service.h:82-84`); the service should own a hidden window of its own so the dock example needs no `attach`. Whether `RegisterShellHookWindow` accepts a message-only window is unknown (section 10); if not, the service creates a 1x1 hidden top-level window |
| Clipboard history | engine: new feature `clipboard` | `S.Clipboard.history` (text and image entries, in memory, `limit`), `copy(index)`, `clear()`, `pin(index)`. `AddClipboardFormatListener` and `WM_CLIPBOARDUPDATE` (documented) on the hidden window; `QClipboard` reads the content. Never listens when `liveDesktop("clipboard")` is false. Second priority: the launcher does not need it |
| File search | example QML | `Process { program: "es.exe" }` (Everything) with `stream: true`; Windows Search through OLE DB is out of scope |
| Calculator, unit conversion, emoji picker, snippets | example QML | pure JS providers in `examples/shells/launcher/providers/` |
| Power menu, OSD, notification stack | example QML | `ControlCenter` already exposes the session actions through its service; the OSD binds to `AudioOutput` and `Brightness`; the notification stack needs a `toastAdded(toast)` signal on `Notifications`, which is a small addition to that feature, listed in the migration |
| Launcher provider protocol | example QML convention, not an engine type | an object with `function query(text, limit)` returning `[{name, detail, icon, run}]`; the launcher concatenates providers in order |

Fail-soft and live-desktop rules apply to every row: a missing COM interface or a failing call
logs once and leaves the list empty; nothing touches the machine on the offscreen platform.

### 4.5 Styling

The styling proposal stands (O2 rich tokens plus O3 style bundles, O1 only as the owner's dev
loop, O4 rejected). This proposal adds what the user-defined shell needs on top:

- **Building blocks are public and composable.** `Island` leaves `BarLayout.qml:65-158` and
  becomes a type of `wabar`; `PopupFrame` and `TooltipFrame` are the chrome of
  `PopupWindow.qml:122-153` and `Tooltip.qml:101-129` as items. Each has `styleClass`, `variant`
  and a replaceable `background` component as the styling proposal describes for parts. A user
  `Overlay` composes them (the launcher example uses `PopupFrame`).
- **Surfaces are style classes.** `panel`, `overlay`, `popup`, `tooltip`, with the surface `name`
  as the variant key: `"panel.side": { fill: ... }`, `"overlay.launcher": { radius: 16 }`.
- **Orientation is a token input.** `Pill` reads `Theme.style(["pill", vertical ? "pill.vertical"
  : ""])`, so a theme can give vertical pills their own padding.

The six open questions of the styling proposal, re-answered for this goal:

1. **Disk-loaded built-ins:** owner only, behind `--modules-dir`. Users fork with `wabarc eject
   <Widget> [<NewName>]`: the bar copies the built-in's source out of its own resources
   (`qrc:/qt/qml/wabar/widgets/<Widget>.qml`, no `dist/` needed) into `<config home>/widgets/<NewName>.qml`,
   with a header comment naming the origin and the module version, and answers the path. The copy
   has a new name, so the shadowing problem (styling proposal 2.2) never arises; the copy keeps
   `import wabar.services as S` and resolves sibling built-ins through `import wabar.widgets`, which
   it must add if the original relied on module-internal resolution. Built-ins remain compiled in
   and are also shipped as loose files in `dist/wabar/modules/` (owner decision 7 in the
   architecture proposal) for `qmllint`, `qmlls` and reading. Release never loads the loose copy.
2. **String keys or typed groups:** as proposed there (typed palette and metrics, a map for
   classes and variants). Surface names as variant keys need the map.
3. **Default palette:** neutral defaults in the engine; the owner's Frappe values move to
   `examples/shells/bar/theme.qml`.
4. **`startFill` aliases:** drop them with the 2.0 break.
5. **Light and dark:** not an engine feature now. `Desktop.darkMode` (section 4.3) lets
   `theme.qml` bind `text: Desktop.darkMode ? "#fff" : "#111"`, which covers the need in user
   space; named theme files come later.
6. **Startup cost of disk-loaded built-ins:** release always runs the compiled copy; the measure
   (styling proposal step 3) decides only whether `--modules-dir` is comfortable for the owner.

### 4.6 Distribution and structure

**The config home is a shell folder.**

```
shell.qml        the Shell: surfaces and parts
theme.qml        ThemeSpec (unchanged)
widgets/         user widgets (unchanged), directory import
surfaces/        user surface files (an Overlay per file), directory import; optional
assets/          images, fonts; optional
```

`--config-home` and `WABAR_CONFIG_HOME` keep their names; a rename buys nothing. A shell folder is
self-contained and shareable by copying; `wabarc check <dir>` validates one without a running
bar's state (it runs in the running bar's throwaway engine) and `wabar.exe --check <dir>` does the
same without a bar and exits 0 or 1, for CI of a shared shell.

**Entry points.** One, `shell.qml`. A large shell splits itself with directory imports
(`import "surfaces"`), which the watcher already covers (`bar_host.cpp:358-359`).

**Errors.** Validation stays transactional for the whole shell: a broken file keeps the previous
shell and shows the error, now in every surface window's error pill. Building is per surface
(4.1.3): one surface whose window cannot be created does not take the others down. The fallback
shell (`FallbackShell.qml`: one top panel with the error) appears only when nothing could be built.

**Safe mode.** `--safe` builds `SafeShell.qml` (today's `SafeConfig.qml` content: one top panel) and
reads nothing from the config home, so no user hotkey, handler or overlay exists in safe mode.
`wabarc surface list` then shows `top` only.

**Hot reload with state.** `Persist { property string query }` is a part whose property values
the host stores in a map keyed by surface name and property name before teardown and writes back
after rebuild; `store: true` also writes them to `%LOCALAPPDATA%\wabar\persist.json`. The host
also remembers which overlays were shown and re-shows them after a reload, so editing the launcher
while it is open keeps it open. This is QuickShell's `PersistentProperties` (from memory).

**Example shells** in `examples/shells/<name>/`, each a complete config home. `examples/config`
moves to `examples/shells/bar` and `WABAR_SAMPLE_CONFIG_DIR` (`src/app/wabar/CMakeLists.txt:8-9`)
follows.

| Shell | Proves |
|---|---|
| `bar` | the migration of today's config; `Panel.Top`, `PanelLayout`, islands, popups, the parts (`WallpaperSettings`) |
| `sidebar` | `Panel.Left` on all screens, exclusive, vertical `PanelLayout` and `Pill`, popups opening to the right, `Strip`-rooted widgets |
| `dock` | a short non-exclusive bottom panel, `S.Windows`, `hideOnFullscreen`, `Island` as a standalone type |
| `launcher` | `Overlay` with `OnDemand` focus, `Hotkey`, `IpcHandler`, `Persist`, `S.Apps`, a `Process`-based provider (Everything), a pure JS provider, `PopupFrame` |

A test loads each example shell headless and checks the surface list it declares.

## 5. Windows feasibility

Column "status" says whether the mechanism is in the tree today (exists), documented but new
(new), or undocumented (allowlist row needed in `docs/architecture.md`).

| Primitive | Mechanism | Status | Risk | Fallback |
|---|---|---|---|---|
| Dock to Top or Bottom with exclusive zone | `SHAppBarMessage` `ABM_NEW`, `ABM_SETPOS` (`app_bar.cpp:36,47`) | exists | the rect `ABM_SETPOS` adjusts is never read back, so two app bars on one edge, or ours next to the Windows taskbar, can overlap | read `data_.rc` after `ABM_SETPOS` and move the window there (`ABM_QUERYPOS` first, as the API describes) |
| Dock to Left or Right | the same with `ABE_LEFT` (0) and `ABE_RIGHT` (2); `AppBarEdge` grows two values | new | the Windows taskbar on the same edge pushes us inward; acceptable | `exclusive: false` |
| Thickness and length on a DPI-scaled or mixed-DPI setup | `computeAppBarRect` scales the height by `dpr` but takes the origin from logical geometry (`app_bar.cpp:8-23`); `SHAppBarMessage` wants physical pixels | exists, suspect | wrong rects on a 150 % secondary monitor | compute every rect from `QScreen::geometry()` times `devicePixelRatio()` on both axes; spike S2 |
| Layer Top | `WS_EX_TOPMOST` (`app_bar.cpp:39`), `Qt::WindowStaysOnTopHint` | exists | topmost panels show over full-screen apps | `hideOnFullscreen` through `Desktop.foregroundFullscreen` |
| Layer Bottom | `Qt::WindowStaysOnBottomHint`: the Qt docs say it works on Windows for frameless or full-screen windows, which every surface is | new | may sit under the desktop icons' window on some builds | `Normal` |
| Layer Desktop (behind icons) | `SetParent` onto WorkerW (`src/features/wallpapers/win32/workerw.h:34`, allowlisted at `docs/architecture.md:216`) | exists, undocumented | Explorer restarts drop the child; not exercised live on build 26300 | not offered as a `Surface.layer` value in this proposal (question 7) |
| Layer Overlay (raised on show) | `SetWindowPos(HWND_TOPMOST)` on `show()` | new | none | none needed |
| Focus NoFocus | `WS_EX_NOACTIVATE` (`app_bar.cpp:38`) plus `Qt::WindowDoesNotAcceptFocus` (Qt docs: on Windows this also keeps the window out of the taskbar) | exists | Mica tracks activation, so a never-active window shows the inactive Mica tint | document; `Blur` or `Acrylic` for panels that must look "active" |
| Focus OnDemand from a hotkey | `requestActivate()` after `WM_HOTKEY` on the GUI thread | new | the foreground lock (`SetForegroundWindow` is refused unless the caller received the last input event, owns the foreground, or was granted by `AllowSetForegroundWindow`); the gallery works today through the hotkey, which suggests the hotkey counts, but it is not proven | `forceForegroundFocus` (`window_focus.h:11`, `AttachThreadInput`, moved to the kernel); spike S1 |
| Focus OnDemand from `wabarc` | `AllowSetForegroundWindow(barPid)` in `wabarc` before the command | new | `wabarc` itself must be the foreground process or have been granted; from a console or a hotkey tool it is | `forceForegroundFocus`; spike S1 |
| Exclusive keyboard (layer-shell "exclusive") | `SetWindowsHookEx(WH_KEYBOARD_LL)` | not offered | a global keyboard hook in a bar is invasive and slows every keystroke | `OnDemand` only; stated as impossible-by-choice |
| Input PassThrough | `Qt::WindowTransparentForInput` (used by `Tooltip.qml:89`) | exists | none | none needed |
| Input region (partial click-through) | `QWindow::setMask` (Qt docs: a hint; on Windows it is `SetWindowRgn`) | new | `SetWindowRgn` disables DWM rounded corners and may break Mica and Acrylic on that window | not offered in 2.0; a shell wants either a full window or pass-through; spike S4 if a use case appears |
| Backdrop Blur | `SetWindowCompositionAttribute` accent 3 (`backdrop.cpp:96`, allowlisted) | exists, undocumented | may stop working on a future build | plain `background` |
| Backdrop Acrylic | `DWMWA_SYSTEMBACKDROP_TYPE` (38) = 3 on build 22621+ (documented); accent 4 (`backdrop.cpp:28`) below that | new (one constant) | needs `DwmExtendFrameIntoClientArea(-1)` as Mica does (`backdrop.cpp:118`); on a NoFocus window the same inactive tint as Mica | Blur |
| Backdrop Mica | `backdrop.cpp:114-130` | exists | inactive tint on NoFocus windows | Blur |
| Show, hide, toggle, animation | `QWindow::setVisible`; the content animates `opacity`; `hideDelay` | new | none; a translucent window already has an alpha swap chain (`main.cpp:22`) | none needed |
| Dismiss on blur | `QWindow::active` false (`PopupWindow.qml:94-99`) | exists | only for OnDemand surfaces; a NoFocus overlay (an OSD) cannot know | timer-based auto-hide in the content |
| Dismiss on Escape | `Keys.onEscapePressed` on a focused item (`WallpaperGallery.qml:106`) | exists | only when focused | none needed |
| Per-screen instantiation, monitor handle | `QGuiApplication::screens`, `MonitorFromWindow` (`bar_host_win32.cpp:24-27`) | exists | none | none needed |
| Screen added, removed, geometry change | `QGuiApplication::screenAdded/screenRemoved/primaryScreenChanged`, `QScreen::geometryChanged` (verified) | new | a rebuild drops popups and `Persist` restores state | none needed |
| Full-screen app detection | `SetWinEventHook(EVENT_SYSTEM_FOREGROUND)` + `GetWindowRect` equals monitor rect (documented) | new (duplicate of the taskbar's hook) | a borderless-window game is a full-screen window; a video in a browser is not | the user adds a `hideOnFullscreen: false` |
| Session lock | `WTSRegisterSessionNotification` (documented) | new | none | none needed |
| Global hotkey | `RegisterHotKey` (documented, `wallpaper_hotkey_win.cpp:24`) | exists | `Win+<key>` reserved; a combination another program holds fails; no per-app scope | `registered` false, log once; the user binds `wabarc surface toggle` in whkd, komorebi or AutoHotkey |
| Start menu apps | `.lnk` under the Start Menu folders, `IShellLink` (documented) | new | shortcuts to installers and uninstallers pollute the list | ignore names matching "uninstall", hidden files; the user filters further in QML |
| Store apps | `shell:AppsFolder` via `SHGetKnownFolderItem(FOLDERID_AppsFolder)` (documented) | new | enumeration takes hundreds of milliseconds on a large machine (not measured) | run on a worker, show shortcuts first; spike S5 |
| Launch, run as admin, open location | `ShellExecuteEx` (`open`, `runas`), `activateAppByAumid` (exists), `SHOpenFolderAndSelectItems` (documented) | mostly exists | UAC prompt for `runas` is modal to the desktop, fine | none needed |
| App icons | `IShellItemImageFactory` through `shellItemIcon` (exists) | exists | COM must be initialised on the provider's thread | `QQuickAsyncImageProvider` with its own pool and a per-thread apartment, as the Qt docs recommend for slow providers |
| Window list and activation | `EnumWindows`, shell hook, WinEvent hooks (exist in taskbar) | exists | the shell hook needs a top-level window; a message-only window may not receive it | the service creates its own hidden top-level window; spike S6 |
| Clipboard listener | `AddClipboardFormatListener` (documented) | new | large images in history eat memory | cap by count and bytes |

Honest list of what Wayland layer-shell has and Windows does not: exclusive zones off-edge, a
keyboard-exclusive mode, a namespace-based layer, an unconditional "this window is a panel" hint
that keeps full-screen apps and the taskbar in line. The fallbacks above are what a user gets.

## 6. Folder and unit layout, rule changes

| Where | New or changed | Unit rules |
|---|---|---|
| `src/host/qml/` | `shell_config.h` (`Shell`, `Surface`, `Panel`, `Overlay`), `surface_controller.{h,cpp}`, `shell_host.{h,cpp}` (from `bar_host`), `hotkey_qml.h`, `ipc_handler.h`, `persist.h`, `desktop.h`; QML: `PanelWindow.qml`, `OverlayWindow.qml`, `PanelLayout.qml`, `Island.qml`, `PopupFrame.qml`, `TooltipFrame.qml`, `FallbackShell.qml`; `PopupWindow.qml`, `Tooltip.qml` edited | S4 satisfied (`qml/`); S7 names unique |
| `src/host/` root | `hotkeys.h` (interface, `parseHotkey`), `desktop_events.h` (interface), `app_bar.h` (four edges), `popup_placement.h` (four edges) | pure, Qt Core and Gui only |
| `src/host/win32/` | `hotkeys_win.cpp`, `desktop_events_win.cpp`, `app_bar.cpp` (four edges, physical rects, read-back), `backdrop.cpp` (Acrylic), `bar_host_win32.cpp` -> `shell_host_win32.cpp` (hooks: `setNoActivate`, `setLayer`, `activate`) | S3 satisfied |
| `src/host/testing/` | `shell_fixtures.h` (from `bar_fixtures.h`), `fake_hotkeys.h`, `fake_desktop_events.h` | S5 |
| `src/control/` | no new code: the verbs are strings; `control_handler` stays in host because it needs the host | S2: host includes `control/` (allowed) |
| `src/kernel/win32/` | `window_focus.*` (users: host overlays, wallpapers), `shell_icons.*` (`shellItemIcon`; users: taskbar via `desktop_windows`, apps), `message_window.*` (a hidden `HWND_MESSAGE` window with a callback; users: clipboard, desktop events, desktop_windows if S6 allows) | each row names its two users, as `AGENTS.md` demands |
| `src/features/apps/` | new: `catalog.h`, `rank.{h,cpp}`, `frecency.{h,cpp}`; `win32/start_menu_win.cpp`, `apps_folder_win.cpp`, `launch_win.cpp`; `qml/apps_service.{h,cpp}` (`Apps`, `image://appicon`); `testing/fake_app_catalog.h`; `tests/`; `design.md` | `DEPENDS` none |
| `src/features/desktop_windows/` | new, extracted from `taskbar`: `window_system.h`, `window_tracker.*`, `app_identity.h`, `win32/window_system_win.cpp`, `window_tracker_hooks_win.cpp`, `app_identity_win.cpp`; `qml/windows_service.{h,cpp}` (`Windows`); `testing/fake_window_system.h`; tests moved | `taskbar` gets `DEPENDS desktop_windows`; `add_subdirectory(desktop_windows)` above `taskbar` in `src/features/CMakeLists.txt` |
| `src/features/clipboard/` | new: `clipboard_store.{h,cpp}` (ring, pure), `win32/clipboard_listener_win.cpp`, `qml/clipboard_service.{h,cpp}`, `testing/`, `tests/`, `design.md` | `DEPENDS` none |
| `src/features/shell/` | `Process` extensions | unchanged deps |
| `src/features/wallpapers/` | hotkey code removed; `WallpaperSettings.hotkey` uses the host interface through QML (`Hotkey` declared by the part) | S2: a feature may not include `host/` C++; the part creates a `Hotkey` QML object instead, which is a QML-level use of `wabar`, allowed for every feature (`docs/architecture.md:45`) |
| `src/features/notifications/` | `toastAdded` signal | unchanged |
| `src/ui/` | `Strip.qml`; `Pill.qml` gains `vertical`, `rotateText`, `styleClass` | unchanged |
| `src/app/wabar/` | `SafeShell.qml` (from `SafeConfig.qml`), `--check`, `--modules-dir`, `--no-hotkeys`, `--no-clipboard` (live-desktop capability flags) | |
| `src/app/wabarc/` | pass-through verbs, `AllowSetForegroundWindow`, help text | `app/` may include Windows headers (S3) |
| `examples/shells/{bar,sidebar,dock,launcher}/` | the four shells; `examples/config` removed | not checked by S1-S8 (outside `src/`) |
| `tools/check.ps1` | line 302: every non-host `.qml` is `wabar.widgets`; still true. S6's type map (`qmlOwner`, line 314) is built from `features/*/qml` only, so host types stay free for every feature; still true | no change |
| `tools/hooks/commit-msg`, `AGENTS.md` | scopes `apps`, `desktop_windows`, `clipboard` | |

Rule changes needed: none of S1-S8 breaks. Two clarifications to write into `AGENTS.md` and the
rule table:

- S2 addendum: `host/` may include `kernel/` (already true) and nothing under `features/`; the
  full-screen detection is therefore duplicated between `host/win32/desktop_events_win.cpp` and
  `features/desktop_windows/win32/window_tracker_hooks_win.cpp`, and the architecture document
  says so in its threading table.
- A new convention, not a rule: QML files of the `wabar` module whose name ends in `Window.qml`
  and the two recovery shells are internal; the `qmldir` marks them with `internal` (the Qt
  mechanism to verify, section 10) and the docs list the public types (4.2.1).

Threads and singletons added, each with its row in `docs/architecture.md`: the `apps` scan task
(`QThreadPool`, no thread of its own), the `appicon` provider's pool, the `desktop_windows`
tracker (moved row), the `Desktop` singleton (WinEvent hook callback on the GUI thread, WTS
notifications through the message window), the `Apps`, `Windows` and `Clipboard` singletons, and
the host's hotkey host (GUI thread, native event filter, as the wallpaper one is described).

## 7. Migration

Every step ends with `tools\check.ps1` green and is one commit or a short series. Sizes: S under
300 changed lines, M 300 to 1,000, L over 1,000, including tests. "Parallel" names steps that can
run in their own worktree at the same time; shared files are `docs/architecture.md`,
`src/features/CMakeLists.txt` and the example shells, which merge line-locally.

| # | Step | Size | Parallel with | Notes |
|---|---|---|---|---|
| 1 | Surface model: `Shell`, `Surface`, `Panel` (Top and Bottom only), `Overlay` (type only, not yet built), `SurfaceController`, `ShellHost`, `PanelWindow.qml`, per-surface build isolation, `Window.window.surface`; delete `Config`, `Bar`, `BarLayout` names (`PanelLayout` is `BarLayout` renamed); `examples/shells/bar`; module version 2.0; tests ported from `test_bar_host.cpp` | L | none; everything else builds on it | the breaking commit; `Pill.flat`, popups and islands updated to `surface` |
| 2 | App bar on four edges with physical rects and read-back; `PanelWindow` geometry for Left and Right, `length`, `alignment`, `margins`; `PopupPlacement` Left and Right; popups and tooltips follow the edge | M | 5, 6, 8, 9, 10, 11 | spike S2 first |
| 3 | Vertical layout: `Strip`, `PanelLayout` orientation, `Island` as a type with transposed drawing, `Pill.vertical`; `Workspaces`, `Taskbar`, `Tray` on `Strip`; `examples/shells/sidebar` | M | 5, 6, 8, 9, 10, 11 | **a vertical bar is possible after steps 1-3** |
| 4 | Overlays built: `OverlayWindow.qml`, show/hide/toggle, `focus`, `input`, `layer`, `dismissOnBlur`, `dismissOnEscape`, `hideDelay`, `fullscreen`, `anchors`; `window_focus` to the kernel; `Overlay` remembered across reload | M | 5, 6, 8, 9, 10, 11 | spike S1 first; the wallpaper gallery becomes an `Overlay` declared by `WallpaperSettings` in a later cleanup |
| 5 | `Hotkey` type, host interface, Win32 host, fake; wallpapers hotkey migrated; `--no-hotkeys` | M | 2, 3, 4, 8, 9, 10 | |
| 6 | IPC registry: `surface list|show|hide|toggle|status`, `IpcHandler`, `call`, `check`; `wabarc` pass-through and `AllowSetForegroundWindow` | M | 2, 3, 4, 5, 8, 9, 10 | |
| 7 | `Persist` part, `store: true`; `toastAdded` on `Notifications` | S | after 1; with anything | |
| 8 | `Process` extensions | S | with anything | |
| 9 | `apps` feature: catalogue, ranking, frecency, launch, `image://appicon`, fakes, tests | L | with anything after 1 | spike S5 first; the launcher's data half |
| 10 | `desktop_windows` extraction and `S.Windows`; own hidden window for the tracker | M | with anything after 1 | spike S6 first |
| 11 | `clipboard` feature | M | with anything after 1 | optional for the launcher |
| 12 | `Desktop` singleton: screens rebuild, full-screen, session lock, work area, dark mode; `Panel.hideOnFullscreen` | M | after 1; with 8-11 | |
| 13 | `examples/shells/launcher` with providers; `examples/shells/dock`; a headless test per example shell | M | after 4, 6, 9 (and 10 for the dock) | **the launcher example is possible after steps 1, 4, 6 and 9**; a hotkey from step 5 is nicer but `wabarc surface toggle` bound in whkd works without it |
| 14 | Styling: the styling proposal's steps 4-10 with `PopupFrame`, `TooltipFrame` and the surface classes | L | after 3; with 9-13 | |
| 15 | `wabarc eject`, `--modules-dir`, `--check`; `docs/styling.md` | M | after 14 | |
| 16 | Docs: `docs/architecture.md` (units, threads, singletons, allowlist rows for nothing new unless S1 needs `AttachThreadInput` listed), `docs/building.md`, `SECURITY.md`, `src/app/README.md`, `AGENTS.md` scopes, feature `design.md` files, this plan's status in `docs/plans/README.md` | M | runs along every step; closed last | |

Order for one agent: 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 12, 10, 11, 14, 15, 16. With three
worktrees after step 1: lane A 2, 3, 4; lane B 5, 6, 7, 8; lane C 9, 10, 11; then 12, 13, 14, 15,
16 in one lane with the merge train the architecture proposal used (section 9 there).

## 8. Alternatives considered and rejected

| Option | For | Against | Verdict |
|---|---|---|---|
| Embed an existing shell framework (QuickShell, AGS/Astal, eww) | proven surface models | all three are Wayland or X11 (layer-shell, GTK layer shell); none builds on Windows; QuickShell's Qt Quick parts are the ideas this proposal borrows, not code it can link | rejected; borrow the API shape |
| A separate process per surface | crash isolation; a launcher that hangs does not freeze the bar | every service becomes a daemon with its own IPC (the tray hook can exist once per machine, `docs/architecture.md:180`); one QML engine per process, so no shared `Theme` object; memory and start-up multiply; the control pipe becomes a bus | rejected; one engine, one process; a surface that hangs is a user bug visible in `wabarc status` |
| A scripting language other than QML for the shell (Lua as in AwesomeWM, JS as in AGS) | smaller runtime, familiar | the config is already QML and QML already embeds JavaScript (`Function("return " + q)` in the launcher example); a second language needs a second binding layer to every service | rejected |
| Keep `Bar` fixed, add the launcher as a built-in C++ feature | shortest path to one launcher | the owner's goal excludes it; the next surface (dock, OSD, power menu) is C++ again; the gallery already shows the cost of a hand-made surface per feature | rejected |
| One full-screen transparent click-through window per screen, every surface an item inside it | one window, trivial z-order between surfaces | app bar reservation needs a window per edge; partial click-through needs `setMask`, which breaks DWM effects; one focus state for everything, so a launcher and a bar cannot differ | rejected |
| `QtQuick.Controls` `Popup`/`Overlay` inside the bar window | no extra windows | the bar window is as tall as the bar (`docs/plans/2026-10-28-qt-quick-bar-config-design.md:159-160`); a launcher does not fit in it | rejected; already decided for popups |
| Expose raw `Window` with user-set flags instead of `Surface` | maximal freedom | every user copies the gallery's 30 lines of flags, screen pick, activation and dismissal; the engine cannot register app bars, hotkeys or IPC names for a window it does not know | rejected; `Surface` is thin on purpose |
| Register every `Overlay` as an app bar too | one code path | an overlay must not reserve space | rejected |

## 9. Open questions for the owner

1. **Root and file name: `Shell` in `shell.qml`?** Recommended: yes. `Config` and `bar.qml` say
   "a bar"; the file is the shell now. The fallback error names the rename.
2. **Module version 2.0 for the break, or stay at 1.0 and break silently?** Recommended: 2.0.
   The version is free, it dates the break in every error message, and the semver rule in 4.2.1
   starts meaning something.
3. **Feature name for the window list: `desktop_windows`?** Recommended: yes, as the architecture
   proposal already named it; `windows` reads like the OS folder.
4. **Should the launcher get exclusive keyboard input (a low-level keyboard hook)?** Recommended:
   no. `OnDemand` focus is what every Windows launcher does; a global hook is the one thing that
   makes a bar feel like malware.
5. **Hotkeys in the engine, or only `wabarc` bound in whkd, komorebi or AutoHotkey?** Recommended:
   both. `Hotkey` costs one file pair and already exists for wallpapers; the honest limits (reserved
   `Win` combinations, first come first served) go in the docs.
6. **Is clipboard history in scope now?** Recommended: after the launcher (step 11 is optional in
   the order of section 7). It needs the engine, so it cannot be an example, but nothing in the
   launcher depends on it.
7. **Offer `layer: Surface.Desktop` (WorkerW) publicly?** Recommended: not in 2.0. It is the one
   undocumented mechanism in the table, it is not exercised on this build, and the wallpaper
   transition is its only user. Revisit when that transition is ported to a Qt Quick window
   (owner decision 1 of the architecture proposal).
8. **Move `examples/config` to `examples/shells/bar` and make it the development default?**
   Recommended: yes; the four shells read as one set and the tests load each.
9. **Animation API: `hideDelay` on the surface, or an engine-driven show/hide animation?**
   Recommended: `hideDelay`. The content owns its animation in QML; the engine only waits. An
   engine animation would be a fixed set of effects, which is class C work the user cannot change.
10. **Error isolation: transactional validation of the whole shell plus per-surface build
    isolation, or per-surface validation?** Recommended: the former. One throwaway engine per
    reload is simple and already tested; per-surface validation would need the shell split into
    files by rule.
11. **The owner's Frappe palette: engine default or example theme?** Recommended: example theme
    (`examples/shells/bar/theme.qml`), as the styling proposal's question 3 recommends.

## 10. What could not be determined

Nothing was built or run for this proposal. Each item names the smallest spike, a throwaway
program or a guarded test, that settles it before the step that depends on it is sized.

| Unknown | Why it matters | Spike |
|---|---|---|
| S1: does `requestActivate()` after `WM_HOTKEY` give a tool window keyboard focus while another app is in front, and does `AllowSetForegroundWindow` from `wabarc` make the `wabarc` path work? | the launcher's focus mode (step 4, 6) | a 60-line Qt program: `RegisterHotKey` alt+space, on the key show a frameless tool `Window` with a `TextField`, call `requestActivate()`, type; repeat with `forceForegroundFocus`; repeat from a second console process that calls `AllowSetForegroundWindow` then sends a pipe command. Run with Notepad in front. Record which variant focuses |
| S2: app bar rects on a mixed-DPI setup and on the taskbar's edge | step 2 | register `ABE_LEFT` with `ABM_SETPOS` on the primary screen at 100 % and on a secondary at 150 %, read `rc` back, compare with `SPI_GETWORKAREA`; `ABM_REMOVE`; run only by hand with `--register-app-bar` and never in `ctest` |
| S3: Mica and Acrylic (`DWMWA_SYSTEMBACKDROP_TYPE` 2 and 3) on a `WS_EX_NOACTIVATE` frameless tool window on build 26300 | the backdrop enum and the "inactive tint" note | apply both to a bar window and to an activated overlay; screenshot; note whether `DwmExtendFrameIntoClientArea(-1)` is required for Acrylic |
| S4: `QWindow::setMask` with a DWM backdrop | whether an input region can ever be offered | set a mask on a Mica window; observe corners and backdrop |
| S5: `shell:AppsFolder` enumeration time and item count on the owner's machine; `.lnk` count under both Start Menu folders | step 9 sizing and whether the catalogue can be built at start or must be lazy | a console program that enumerates both and prints counts and milliseconds |
| S6: does `RegisterShellHookWindow` deliver `HSHELL_*` messages to a message-only (`HWND_MESSAGE`) window? | whether `S.Windows` needs a visible top-level window of its own (step 10) | register from a message-only window and from a 1x1 hidden top-level window; open and close Notepad; count messages |
| Whether `QT_QML_INTERNAL_TYPE` (or the `qmldir` `internal` line) hides `PanelWindow` and friends from `qmllint` completion and from `import wabar` in a config | the public/private split of 4.2.1 | set it on `PanelWindow.qml` in step 1 and try to declare one in a test config |
| Whether `Qt::WindowStaysOnBottomHint` keeps a frameless window under the desktop icons or over them on build 26300 | the `Surface.Bottom` layer | one window with the hint, next to the desktop |
| The cost of rebuilding every surface on a screen change versus moving windows | `Desktop.screensChanged` handling (step 12) | measured after step 1 with two panels and the launcher; if the rebuild takes under 300 ms it stays |
| Whether `IShellItemImageFactory` is safe from a `QQuickAsyncImageProvider` pool thread with an MTA apartment, which `ensureWinrtApartment()` (`docs/architecture.md:95`) would give | the `appicon` provider (step 9) | request ten icons from the pool in a test with the live gate off, using fake `.lnk` files |
| How many of the 13 `Pill`-rooted widgets look acceptable vertically with only the `Pill` change | step 3 scope | the sidebar example, viewed once by the owner after step 3; widgets that need more get their own rows in the follow-up |

Qt 6.10 behaviour checked in the documentation for this proposal: the meaning of `Qt::Tool`,
`Qt::ToolTip`, `Qt::Popup`, `Qt::WindowStaysOnTopHint`, `Qt::WindowStaysOnBottomHint` (Windows:
frameless or full-screen only), `Qt::WindowTransparentForInput` and `Qt::WindowDoesNotAcceptFocus`
(Windows: also hidden from the taskbar); `QQmlEngine::clearComponentCache` (unreferenced
components only; existing objects and singletons keep their types), `trimComponentCache`,
`clearSingletons`; `addImportPath` goes first in the list, `setImportPathList` drops the defaults;
`QQuickImageProvider` with `ForceAsynchronousImageLoading` runs on one thread per engine, and
`QQuickAsyncImageProvider` is the way to use a pool; `QWindow::setMask` is a hint to the window
system; `QGuiApplication::screenAdded`, `screenRemoved`, `primaryScreenChanged`;
`QQuickWindow::setDefaultAlphaBuffer`; `Window.transientParent` (a window declared inside an item
waits for its parent to show; `null` overrides), `Window.screen` assignable from
`Application.screens`, `requestActivate`; `ColumnLayout`, `ListView.orientation`; `QProcess`
`readyReadStandardOutput`, `setProcessEnvironment`, `terminate`, `kill`, and the setters
`startDetached` honours.

## 11. Revision 2026-10-30: Quickshell-exact vocabulary and the caelestia port

The owner added a goal: users port the supported functionality and styling of existing Quickshell
shells to Windows. The first reference is https://github.com/caelestia-dots/shell. This section
records what that changes in this proposal. Where it conflicts with sections 4 to 7, this section
wins. Nothing here is decided until the owner answers questions 12 to 15.

### 11.1 What changes

- **Goal and non-goals.** Section 2 says "Parity with QuickShell" is a non-goal. It becomes: source
  compatibility with Quickshell QML for the subset that has a Windows meaning, with the exclusions
  listed at the end of this section. "A plugin ABI for C++ extensions" stays a non-goal as a promise,
  but the engine loads user-built Qt QML plugins from the config home (11.3, item 5).
- **Names.** Sections 4.1.1 and 4.2.2 rename Quickshell's types. Ported QML would fail on its first
  line. The vocabulary becomes Quickshell's own; `Panel` and `Overlay` stay as presets over
  `PanelWindow`. Questions 1 and 2 are answered by this: the root is `ShellRoot`, in `shell.qml`.

| Section 4 name | New name |
|---|---|
| `Shell` | `ShellRoot` (and `Scope` for grouping) |
| `Surface`, `Panel`, `Overlay` | `PanelWindow` (creatable), `FloatingWindow`; `Panel` and `Overlay` remain as presets |
| `edge`, `thickness`, `length`, `exclusive` | `anchors.{top,bottom,left,right}`, `implicitWidth/Height`, `exclusiveZone`, `exclusionMode` |
| `screens: [...]` | `Variants { model: Quickshell.screens }`; `screens` stays as a shorthand on the presets |
| `Window.window.surface` | `QsWindow.window` (attached) |
| `Persist` | `PersistentProperties { reloadableId }` |
| `Hotkey { keys }` | `GlobalShortcut { name, appid, description }`; `Hotkey` stays as the key-owning form |
| `Process { program, stream, line(), json }` | `Process { command, running, stdout: StdioCollector or SplitParser, stderr, environment, workingDirectory, exec(), exited(exitCode, exitStatus) }` |
| `Shell.env`, `Shell.start` | `Quickshell.env()`, `Quickshell.execDetached()` |
| `Desktop.screens` | `Quickshell.screens` of `ShellScreen` (`name`, `width`, `height`, `x`, `y`, `model`, `serialNumber`) |
| `anchorItem`, `anchorRect` | `anchor.item`, `anchor.rect`, `anchor.edges`, `anchor.gravity` |
| `Loader { active }` for lazy surfaces | `LazyLoader { loading }` |
| `Apps` | `DesktopEntries`, `DesktopEntry` (`execute()`, `icon`, `categories`) |
| `S.Windows` | `ToplevelManager.toplevels` (plus `S.Windows` as the wabar form) |
| `focus: Surface.OnDemand` | `WlrLayershell.keyboardFocus: WlrKeyboardFocus.OnDemand`, through a `Quickshell.Wayland` shim |
| `input: PassThrough` | `mask: Region {}` (empty region); also `Surface.PassThrough` |

`IpcHandler` already has Quickshell's form and stays.

### 11.2 Input regions come back

Section 5 says an input region is "not offered in 2.0" because `SetWindowRgn` breaks DWM effects.
Caelestia puts every panel into one full-screen window and cuts the click-through shape with a
`Region`. It uses `mask:` 27 times. Without a region implementation, a ported shell needs one window
per panel, which is a rewrite of its `drawers/` module.

Proposed mechanism: no `SetWindowRgn`. While a masked window is shown, poll `GetCursorPos` at 30 Hz,
test the point against the `Region`, and toggle `WS_EX_TRANSPARENT`. Spike S7 measures click loss at
region edges and the cost of the poll. If S7 fails, the documented fallback is one window per panel,
with hover zones as thin edge windows.

The alternative rejected in section 8 ("one full-screen window per screen, every surface an item
inside it") becomes viable for ported shells once S7 passes. App bar reservation still needs one
window per reserved edge, so caelestia's four 1 px `ExclusionZone` windows map onto it unchanged.

### 11.3 Engine additions

| # | Item | Size | What |
|---|---|---|---|
| 1 | Quickshell window vocabulary | M | The rename above, plus `QsWindow`, `Variants`, `Scope`, `ShellRoot`, `LazyLoader`, `FloatingWindow`, `Region`, `ShellScreen`, `ExclusionMode`; a thin `Quickshell.Wayland` module with `WlrLayershell.layer`, `keyboardFocus`, `namespace`, `exclusionMode` as attached properties over the same state. Same engine work as `Surface`; only names move |
| 2 | `mask: Region` | M | 11.2 |
| 3 | `Quickshell.Io` and the `Quickshell` singleton | M | `FileView` (`JsonAdapter`, `FileViewError`), the `Process` shape above, `ElapsedTimer`; `env`, `execDetached`, `shellDir`, `shellPath`, `statePath`, `cachePath`, `dataPath`, `clipboardText`, `processId`, `iconPath`, `reload`. Supersedes the `Process` row of section 4.4 |
| 4 | `GlobalShortcut`-compatible `Hotkey` | S | `name`, `description`, optional `keys`; `wabarc shortcut <name>` fires it, so whkd or AutoHotkey bind it as a Hyprland config does |
| 5 | Plugins from the config home | M | `<config home>/modules` and `--import-path` join the QML import path. A user-built Qt QML plugin loads after a Qt version and build id check. `SECURITY.md` says it runs as the user, as the config does. Unlocks about 70% of caelestia's 20,800-line plugin with no engine code |
| 6 | Material 3 dynamic colour | M | A pure, tested port of material-color-utilities (tonal palettes from an image) and an image luminance helper, in `wallpapers` or the kernel (two users needed for the kernel) |
| 7 | Service gaps | M | `Media.players` (GSMTC has all sessions); per-app audio streams (`IAudioSessionManager2`); Bluetooth discovery and pairing; `Desktop.capsLock`, `numLock`, `keyboardLayout`, `idleSeconds`; an idle inhibitor (`SetThreadExecutionState`); GPU and disk stat sources; `Notifications` fields on `toastAdded` |
| 8 | `layer: Background` | S to decide, M to do | The WorkerW attach exists and is allowlisted (`docs/architecture.md:216`). Caelestia's background module needs it. Revisits question 7 |
| 9 | Capture | L, last | `ScreencopyView` over `Windows.Graphics.Capture`; window previews over DWM thumbnails; WASAPI loopback for `AudioCollector` |

### 11.4 Migration

Item 1 joins step 1, because step 1 is the breaking commit and names are cheapest to change there.
Item 4 joins step 5 and item 3 joins step 8. New steps, in order after step 16:

| # | Step | Size | Notes |
|---|---|---|---|
| 17 | `mask: Region` (item 2) | M | spike S7 first |
| 18 | Plugins from the config home (item 5) | M | spike S8 first |
| 19 | Material 3 colour (item 6) | M | pure; parallel with anything |
| 20 | Service gaps (item 7) | M | one commit per service |
| 21 | `layer: Background` (item 8) | M | after question 14 |
| 22 | Capture (item 9) | L | last; optional |

### 11.5 Windows limits for a port

Out, and the docs must say so: session lock and PAM (`LockWorkStation()` only); being the
notification server and invoking toast actions (Windows owns toasts); DBus tray menus rendered in
QML (the tray app owns its Win32 menu, so `SystemTrayItem.menu` is null); alpha-thresholded
compositor blur (Windows blurs the whole window rectangle, so a rounded blurred panel gets Windows'
corner radius or blurred corners); Hyprland special workspaces and `dispatch`; exclusive keyboard
focus (question 4).

### 11.6 New spikes

| Unknown | Why it matters | Spike |
|---|---|---|
| S7: cursor polling at 30 Hz toggling `WS_EX_TRANSPARENT` as an input region | item 2, caelestia's whole drawers design | a full-screen transparent tool window with a `Region` of three rectangles; click through the gaps and the edges while a second app is in front; record missed and stolen clicks and CPU |
| S8: loading a user-built Qt QML plugin from the config home into `wabar.exe` | item 5 | build a one-type plugin against the same Qt 6.10 ARM64 and x64; load through the import path; load one built against another Qt version and check the guard |
| S9: variable-font axes (`GRAD`, `ROND`, `opsz`, `wdth`, `wght`) under `fontengine=gdi` | caelestia's Google Sans Flex and Material Symbols | render both with and without the GDI font engine; if axes need DirectWrite, offer a font engine option |

### 11.7 New open questions

12. **Adopt Quickshell's exact names for the creatable types (11.1)?** Recommended: yes. It is the
    only way ported QML loads without edits, and the cost is a rename before anything depends on the
    old names. `Panel` and `Overlay` remain as short forms for hand-written shells.
13. **Offer `mask: Region` through cursor polling, pending S7?** Recommended: yes, behind the spike.
    Without it, most large Quickshell shells need a rewrite of their window layout.
14. **Offer `layer: Background` through WorkerW?** Recommended: yes, with the Explorer-restart
    fallback documented, once the wallpaper transition no longer owns that code (reverses the
    recommendation of question 7 for the shell-porting goal).
15. **Amend "no plugin ABI" to "no ABI promise" and load plugins from the config home?**
    Recommended: yes, with the version check. It runs code as the user, as the config does already,
    and it is what lets caelestia's `Config`, `Tokens`, `I18n` and `State` singletons load.
