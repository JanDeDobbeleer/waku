# QML reference

waku exposes QML modules for shells. This is an introduction to the main types and where to find
their complete property metadata. The current development version can differ from a release.

## Modules

| Import | Purpose |
| --- | --- |
| `waku` | Shell windows, panels, layout, themes, shortcuts and other engine types. |
| `waku.widgets` | Built-in widgets such as `Clock`, `Volume`, `Battery` and `Workspaces`. |
| `waku.services` | Data sources and actions used by widgets and shells. |
| `Quickshell` and its submodules | Compatibility types for ported Quickshell shells. |

Widget and service names can overlap. Use `import waku.services as S` and refer to a service as
`S.Name` when also importing `waku.widgets`.

The built-in widgets include `ActiveLayout`, `Battery`, `Bluetooth`, `Calendar`, `Clock`,
`ControlCenter`, `Cpu`, `GitHub`, `Media`, `Memory`, `Notifications`, `Taskbar`, `Tray`,
`Volume`, `WallpaperGallery`, `Wallpapers`, `Wifi` and `Workspaces`. Their QML files in the
deployed `modules\waku\widgets` folder show their properties and usage examples.

## Common types

- `ShellRoot`: root of a shell; holds panels and other objects.
- `Panel`: builds a panel per matching screen. Set `screens`, `edge`, `thickness`, `exclusive`,
  `backdrop`, `background`, `enabled` and `content`. Its edges are `Panel.Top`, `Bottom`, `Left`
  and `Right`. `exclusive` defaults to false.
- `PanelLayout`: arrange items in `start`, `center` and `end`; adjust `spacing`, `padding`,
  `startPadding` and `endPadding`. For island backgrounds it also accepts `startFill`,
  `centerFill` and `endFill`.
- `ThemeSpec`: root of `theme.qml`; set properties such as `text`, `surface`, `accent`,
  `fontFamily`, `fontSize`, `radius`, `padding` and `spacing`. Built-in widgets read `Theme`.
- `Clock`: built-in widget with `format`, `tooltipFormat` and `calendar`.
- `GlobalShortcut`: use `name` and optionally `keys`; handle `onPressed` and `onReleased`.
- `IpcHandler`: declare a `target`, `enabled` and QML functions callable with `wakuc call`.
- `PanelWindow`, `FloatingWindow`, `Region` and `Variants`: lower-level window, input and
  multi-screen building blocks.

See [Customization](Customization) for runnable examples and
[shell author notes](https://github.com/JanDeDobbeleer/waku/blob/main/docs/shells.md) for
Windows and Quickshell differences.

Docked panels on the Top or Overlay layer auto-hide while a fullscreen application covers their
monitor on the current desktop. Move the pointer to the panel's screen edge to reveal it; it
hides again when the pointer leaves the bar and its popup. During fullscreen the bar reserves no
work area, even while revealed. Leaving fullscreen restores its placement and reservation.
Captioned maximized windows do not trigger auto-hide. This also applies to ported `PanelWindow`
shells; no theme changes are needed.

## Complete type and property metadata

The built and deployed module folders contain `qmldir`, `.qmltypes` files and QML source for
tools and editors. They describe the types, properties, signals and methods of that build:

- In a deployed distribution: `modules\waku` and its `services` and `widgets` folders.
- In a source build: `build\<preset>\qml_modules`, including the `Quickshell` modules.

For the implementation and examples, see the
[module and type inventory](https://github.com/JanDeDobbeleer/waku/blob/main/docs/architecture.md#qml-modules-and-built-in-widgets),
[Quickshell module map](https://github.com/JanDeDobbeleer/waku/blob/main/src/quickshell/README.md)
and [sample configurations](https://github.com/JanDeDobbeleer/waku/tree/main/examples).
The metadata for the installed build is preferable to a web page when APIs differ by version.
