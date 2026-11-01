## Notes for shell authors

Short notes on what differs from Quickshell when you write a shell for wabar. The design is in
`docs/architecture.md`.

### Input regions (`mask`)

`PanelWindow` and `FloatingWindow` take a `mask: Region`, as in Quickshell.

```qml
PanelWindow {
    id: win
    anchors { top: true; bottom: true; left: true; right: true }
    mask: Region {
        x: 40; y: 0; width: win.width - 40; height: win.height
        intersection: Intersection.Xor
        regions: panelRegions.instances        // an Instantiator of Region { ... }
    }
}
```

- `Region` has `x`, `y`, `width`, `height` (window coordinates), `item` (the item's bounds, which
  follow the item), `shape` (`RegionShape.Rect` or `Ellipse`), `intersection` (`Combine`,
  `Intersect`, `Xor`, `Subtract`) and child regions (the default property, `regions`). Children are
  folded into their parent in order.
- `mask: Region {}` makes the window fully click-through. No `mask` means the whole window takes
  input. The `intersection` of the mask itself combines it with the window rectangle, as in
  Quickshell: `Combine` (the default) uses the mask alone; `Intersect`, `Xor` and `Subtract` give
  `window <op> mask`. In the example above the window takes input everywhere except inside the
  inner area (minus the open panels), which is how a bar plus its borders plus open drawers is
  masked.
- `PopupWindow` has no `mask`.
- The mask is enforced by polling the cursor (120 Hz while it moves near a masked region, 30 Hz at rest, down to 4 Hz far from every region) and making the window transparent for input
  outside the region, padded by 6 px. Keep two things in mind:
  - A click within a few milliseconds of the cursor reaching a region you did not pad can hit the window below.
    Give targets a few pixels of slack.
  - Hover and drag effects outside the region do not run: the window gets no mouse events there.
- Nothing is enforced on the offscreen platform or in tests.

### Global shortcuts without keys

A Hyprland-style shortcut has a name and no keys; the compositor binds the key. On Windows nothing
binds it, so give the shortcut a name and bind a key to `wabarc shortcut <name>` in a hotkey tool
such as whkd or AutoHotkey:

```qml
GlobalShortcut {
    name: "launcher"            // no `keys`: nothing is registered by wabar
    onPressed: launcher.visible = !launcher.visible
}
```

```ahk
; AutoHotkey v2
#Space::Run('wabarc shortcut launcher', , 'Hide')
```

```
# whkd
win + space : wabarc shortcut launcher
```

`wabarc shortcut <name>` fires `onPressed` and then `onReleased` of every shortcut with that name.
`wabarc shortcut list` prints the names. A shortcut that does set `keys` (`keys: "alt + space"`)
registers the key itself, unless wabar runs with `--no-hotkeys` or the key is taken; it still
answers `wabarc shortcut <name>`.

### Hiding the Windows taskbar (`--hide-taskbar`)

A shell that replaces the taskbar (a bar, a dock) starts wabar with `--hide-taskbar`. wabar then
turns the taskbar's auto-hide on, which releases its work area, and hides its windows on every
monitor, before the first shell is loaded, so a panel that reserves space (`--register-app-bar`)
computes it without the taskbar. It restores the taskbar when wabar exits, and the next start
undoes it if wabar crashed. Without the flag wabar never touches the taskbar. At run time
`wabarc taskbar hide|show|status` does the same; `show` leaves the taskbar visible until `hide`.
If another tool already hides the taskbar, wabar leaves that alone: `status` reports what is seen,
and wabar undoes only what it changed.

### Variable fonts (`--font-engine`)

wabar starts with Qt's GDI font engine, which keeps memory low but ignores variable-font axes
(`font.variableAxes`). A shell that needs axes starts wabar with `--font-engine=directwrite`, at
the cost of about 2 MB more working set. A `QT_QPA_PLATFORM` set by the caller wins over the flag.

### Qt Quick Controls

`import QtQuick.Templates as T` and `import QtQuick.Controls.Basic` load, headlessly too. Only the
Basic style is deployed (no native style); the other styles (`Fusion`, `Material`, ...) are not. A release build adds about 4.2 MB of
DLLs and 0.6 MB of QML for this (`cmake/deploy_imports/DeployImports.qml`).

### `Singleton`, `TransformWatcher` and name shadowing

`Singleton` (a `Scope`) is the root of a `pragma Singleton` file, as in Quickshell. `TransformWatcher`
has `a`, `b`, `commonParent` and a `transform` property whose `transformChanged()` signal tells a
binding that the transform between the two items changed. `import Quickshell` (and
`Quickshell.Io`, `.Services.Mpris`, `.Services.Notifications`) shows Quickshell's type names only: a
`Wallpapers.qml`, `Workspaces.qml`, `Bluetooth.qml` or `Media.qml` next to `shell.qml` is your own
type, never wabar's service of that name. The shell reloads only when a `.qml` file or a `qmldir`
under the config home changes; a state or cache file written there does not reload it.

### Live window previews (`WindowThumbnail`)

`WindowThumbnail` shows another window live, as the Windows taskbar flyout does. It registers a
DWM thumbnail (documented `DwmRegisterThumbnail` family) on the QML window that holds the item.

```qml
WindowThumbnail { hwnd: 12345; width: 320; height: 180 }
```

- `hwnd` is the source window as a number (the `hwnd` of a taskbar window entry); 0 shows nothing.
  `active: false` unregisters it (use it for hidden flyouts). `fit` is `PreserveAspectFit` (default)
  or `Stretch`. `item.opacity` is honoured, ancestors' opacity included.
- `sourceWidth` and `sourceHeight` give the window's size in physical pixels (use them to size the
  card by the window's aspect). `available` says the preview is registered and the source exists;
  when it is false show your own fallback (an icon card).
- The compositor draws the thumbnail above the whole destination window. QML content cannot overlay
  it (a title bar or a close button must sit beside it, not on it), a parent's `clip` does not clip
  it, and `grabToImage()` does not capture it.
- It does nothing on the offscreen platform.

### Redeclaring a property of the engine's types

A Quickshell shell may declare its own property with the name of one the engine already has, and it
loads: the declaration shadows the engine's. This works for `name`, `data`, `screen`,
`contentItem` and `hideDelay` on windows and Scopes, and `data` on a `Singleton` (its default
property is `shellChildren`, so a declared `data` does not take its children). Mind what shadows:
a window that declares `screen` receives `screen: modelData` in its own property and the engine
keeps placing the window on the primary screen. Pass the screen through a differently named property
when the window must follow it.

### Types for ported shells that do little on Windows

`Quickshell.Wayland` has `IdleInhibitor` (holds the machine awake, ignores `window`) and
`IdleMonitor` (`isIdle` after `timeout` seconds without input). `QsMenuOpener` and
`QsMenuHandle` load but never hold entries, because a tray item's `menu` is null.
`Quickshell.Networking` has `wifiEnabled` (assign it to switch the radio). A `PopupWindow`
takes `implicitWidth` and `implicitHeight`.