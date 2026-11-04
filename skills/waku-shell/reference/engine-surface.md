# Engine surface

What a shell can use. Verify a name in the source before you rely on it:
`docs/architecture.md` (section "QML modules and built-in widgets"), `src/quickshell/README.md`
(the Quickshell type table), `docs/shells.md`, `docs/building.md` (flags and `wakuc`).

## Config home

A folder with `shell.qml`, optionally `theme.qml`, `widgets/` (your components, one per file) and
`modules/` (user QML plugins). Chosen by `--config-home <dir>`, then `WAKU_CONFIG_HOME`, then
`%USERPROFILE%\.config\waku`. The shell reloads when a `.qml` file or `qmldir` under it changes.
A broken edit keeps the previous shell and shows an error pill. A state or cache file written
there does not reload it.

## Modules

| Import | Holds |
|---|---|
| `waku` | `ShellRoot`, `Scope`, `Panel`, `PanelLayout`, `PanelWindow`, `FloatingWindow`, `PopupWindow`, `Variants`, `LazyLoader`, `GlobalShortcut`, `Region`, `Theme`, `ThemeSpec`, `Tooltip`, `Backdrop` (`None`, `Blur`, `Mica`), `ScreenList` |
| `waku.widgets` | Built-in widgets (below) |
| `waku.services` | C++ data sources and actions. Import as `import waku.services as S`: names clash with widgets |
| `Quickshell` | The Quickshell types and singleton, `DesktopEntries`, `ElapsedTimer`, `Singleton`, `WindowThumbnail`, `TransformWatcher`, `ScriptModel`, `SystemClock`, `PersistentProperties`, `IpcHandler` |
| `Quickshell.Io` | `Process`, `FileView`, `JsonAdapter`, parsers |
| `Quickshell.Wayland` | `WlrLayershell`, `ToplevelManager`, `IdleInhibitor`, `IdleMonitor` |
| `Quickshell.Widgets` | `IconImage`, `ClippingRectangle`, `WrapperRectangle`, ... |
| `Quickshell.Services.*` | `Mpris`, `Notifications`, `UPower`, `Pipewire`, `SystemTray` |
| `Quickshell.Bluetooth`, `Quickshell.Networking` | Bluetooth adapter and devices; `wifiEnabled` |
| `Quickshell.Hyprland` | Compile compatibility only. Models are empty. Do not build features on it |

`import Quickshell` shows Quickshell's names only. A file of your own named like a waku service is
your type.

## Built-in widgets (`waku.widgets`)

`Clock`, `Calendar`, `Workspaces` (komorebi and GlazeWM), `ActiveLayout`, `Taskbar`, `Tray`, `Media`,
`Volume`, `Bluetooth`, `Wifi`, `Battery`, `Cpu`, `Memory`, `Notifications`, `GitHub`,
`ControlCenter`, `Wallpapers`, `WallpaperGallery`, and the shared `Pill`, `Slider`, `Strip`,
`DeviceMenu`, `DeviceRow`. Source: `src/features/<f>/qml/` and `src/ui/`. After a build,
`dist/waku/modules/` holds copies. Copy one into `widgets/` under a new name to change it.

## Panel presets

```qml
import waku
import waku.widgets

ShellRoot {
    Panel {
        screens: ["primary"]      // "primary", "all", or a name such as "DISPLAY2"
        edge: Panel.Top           // Top, Bottom, Left, Right
        thickness: 40
        backdrop: Backdrop.Blur   // DWM acrylic; None and Mica skip the blur
        content: PanelLayout {
            start: [ Clock {} ]
            center: [ Workspaces {} ]
            end: [ Volume {}, Battery {} ]
        }
    }
}
```

Quickshell form: `Variants { model: ScreenList.screens; PanelWindow { ... } }`.

## Theme

`theme.qml` with `ThemeSpec { ... }`. Properties: `text`, `surface`, `accent`, `warn`, `good`,
`caution`, `fill`, `fillHover`, `fillActive`, `fillUsed`, `fillEmpty`, `popupSurface`,
`popupBorder`, `fontFamily`, `fontSize`, `smallFontSize`, `pillHeight`, `radius` (negative is a
pill), `padding`, `spacing`. Colours are `#aarrggbb`. The built-in widgets use the theme. A shell
with its own components carries its own tokens and does not need it.

## What the engine does for a shell

- Windows: `PanelWindow` (anchors, margins, `implicitHeight`, `exclusiveZone`, `exclusionMode`,
  `layer`, `keyboardFocus`, `backdrop`, `mask: Region`), `FloatingWindow`, `PopupWindow`
  (`anchor.item`). A window with a `name` answers `wakuc surface show|hide|toggle <name>`.
- Work area: `--register-app-bar` lets a panel reserve screen space. `--hide-taskbar` hides the
  Windows taskbar and restores it on exit. Without them waku touches neither.
- Input: `GlobalShortcut` with `keys` registers the key. Without `keys`, bind a key to
  `wakuc shortcut <name>` in whkd or AutoHotkey.
- Commands: `IpcHandler { target; function ... }` answers `wakuc call <target> <function>`.
- Processes: `Process` from `Quickshell.Io`. `elevated` runs as admin (UAC, needs the user).
  `DesktopEntry.execute()` raises a running app; `executeNew()` starts another; `launchAsAdmin`
  and `launchNew` live on `AppsService`.
- Files: `FileView`, `JsonAdapter`. Persist state outside the watched tree or accept no reload.
- Fonts: GDI by default. Variable-font axes need `--font-engine=directwrite`.

## CLI

`waku.exe`: `--config-home`, `--register-app-bar`, `--hide-taskbar`, `--no-tray`, `--safe`,
`--no-hotkeys`, `--font-engine`, `--log-file`, `--manage-komorebi`, `--no-workarea-sync`,
`--import-path`. `wakuc` (give it the same `--config-home`): `status`, `reload`, `quit`,
`surface`, `call`, `shortcut`, `taskbar`, `wallpaper`, `config init`, `startup`. Exit codes of
`wakuc`: 0 ok, 1 error answer, 2 no bar running, 3 usage.

## Not present

No compositor. Hyprland, Niri and Sway data does not exist. Tray menus are null (`QsMenuOpener`
holds no entries). `ToplevelManager` has no `maximized` or `fullscreen`. `UPower` energy and rate
are 0. `PopupWindow` has no `mask`. Check `docs/architecture.md` for the current limits of a type
before you use it.
