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
- The mask is enforced by polling the cursor (120 Hz while it moves, 30 Hz at rest) and making the window transparent for input
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
