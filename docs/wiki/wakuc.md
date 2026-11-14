# wakuc command reference

`wakuc` is the command-line client for waku. Use it to control a running bar, manage startup,
and create a starter configuration. Run the examples in PowerShell after
[installing waku](Installation).

This reference describes the current development version. Your installed release may have different
commands or options. Run `wakuc --help` for the help included with your version.

## Choose an instance

Most commands need a running waku instance. `config init` and `startup` work without one. `wakuc`
selects the config home in this order:

1. `--config-home <dir>`.
2. The `WAKU_CONFIG_HOME` environment variable.
3. `%USERPROFILE%\.config\waku`.

Use the same config home as the instance you want to control. Quote paths and names containing
spaces:

```powershell
wakuc --config-home 'C:\Users\Example\my shell' status
wakuc --config-home 'C:\Users\Example\my shell' surface toggle launcher
```

Different config homes can have separate running instances. `--help` or `-h` prints command-line
help.

## Bar commands

```powershell
wakuc status
wakuc reload
wakuc quit
```

- `status` reports the process ID, window count, safe-mode state and any shell error.
- `reload` reloads the shell configuration and waits for the result.
- `quit` stops the instance.

## Configuration

```powershell
wakuc config init
wakuc config init --force
```

`config init` creates the config home if needed and writes the built-in bar to `shell.qml` as a
starting point. It refuses to overwrite an existing `shell.qml` unless you pass `--force`.
It does not replace other files. The command prints the path it wrote and does not need a running
bar.

## Startup

```powershell
wakuc startup enable
wakuc startup status
wakuc startup disable
```

`enable` registers a per-user Scheduled Task to start waku at sign-in. It also registers a task
for `wakukeys` when `keys.conf` exists in the explicitly selected config home or, if none is
specified, in `%USERPROFILE%\.config\waku`, and `wakukeys.exe` is on PATH. The keys-file check
does not use `WAKU_CONFIG_HOME`; pass `--config-home` when your keys file is elsewhere.
`waku.exe` must be on your user PATH. Run `enable` again to replace the tasks. `status` shows their
state and executable paths; `disable` removes them.

For `enable`, use `--config-home` to pass a config home to waku. `--no-app-bar` prevents panels
from reserving screen space, and `--keep-taskbar` leaves the Windows taskbar alone. Pass other
waku flags after `--`:

```powershell
wakuc --config-home 'C:\Users\Example\my shell' startup enable --no-app-bar --keep-taskbar -- --no-tray
```

An explicit `--config-home` is also passed to the keys task. Without it, waku resolves its config
home when it starts. See
[Run at sign-in](Installation#run-at-sign-in)
for details.

## Wallpaper

```powershell
wakuc wallpaper set 'C:\Pictures\mountains.jpg'
wakuc wallpaper set mountains
wakuc wallpaper random
wakuc wallpaper next
wakuc wallpaper previous
wakuc wallpaper gallery
```

`set` accepts an image path or a filename in the configured wallpaper folder. Folder filenames are
case-insensitive and can omit the extension; ambiguous names produce an error. `random`, `next`
and `previous` select from that folder; `next` and `previous` wrap. `gallery` opens or closes the
picker. The shell configures the wallpaper folder and animation through `WallpaperSettings` or a
`Wallpapers` widget.

Add `--dry-run` to `set`, `random`, `next` or `previous` to play the animation without changing the
wallpaper:

```powershell
wakuc wallpaper set --dry-run 'C:\Pictures\mountains.jpg'
```

## Shell shortcuts

```powershell
wakuc shortcut list
wakuc shortcut launcher
```

`list` prints the names of the shell's `GlobalShortcut` objects. A name fires `onPressed` and then
`onReleased` on every shortcut with that name. Bind this command in a hotkey tool, or in your
`keys.conf`, to trigger a shell action. The name `list` is reserved for listing.
See [shell shortcut notes](https://github.com/JanDeDobbeleer/waku/blob/main/docs/shells.md#global-shortcuts-without-keys).

## Shell surfaces

```powershell
wakuc surface list
wakuc surface show launcher
wakuc surface hide launcher
wakuc surface toggle launcher
wakuc surface status launcher
```

Surfaces are shell windows with a `name`, such as a launcher or menu. `list` shows names and
visibility. The other commands act on all windows with the given name. If any matching window is
visible, `status` reports `shown`; `toggle` hides all of them. Otherwise it shows them all.
`show` and `toggle` allow the bar to take keyboard focus.

## Windows taskbar

```powershell
wakuc taskbar hide
wakuc taskbar show
wakuc taskbar status
```

`hide` hides the Windows taskbar; `show` restores what waku changed. `status` reports the observed
state. On a live desktop, waku normally hides the taskbar at startup and restores its changes on
exit. Starting waku with `--keep-taskbar` leaves it alone until a taskbar command is issued.
The taskbar service is not available on the offscreen platform.

## Virtual desktops

```powershell
wakuc desktop status
wakuc desktop goto 2
wakuc desktop next
wakuc desktop previous
wakuc desktop move 2
```

Desktop numbers start at 1 and must refer to an existing desktop. `goto` switches to it. `next`
and `previous` switch to an adjacent desktop and stop at the ends. `move` sends the focused window
to the specified desktop without switching to it. `status` reports the current number and count.
These commands depend on Windows virtual desktop support.

## Window tiling

```powershell
wakuc tile status
wakuc tile toggle
wakuc tile float
wakuc tile layout columns
wakuc tile layout next
wakuc tile retile
```

The tiler is enabled by default on a live desktop. A shell can turn it off, or waku can start with
`--no-tiling`.

On load or shell reload, the tiler restores and tiles eligible maximized windows without taking
focus. Windows on another virtual desktop wait until that desktop is shown. Minimized windows and
explicit ignore or float rules stay untouched. Maximizing a window afterward keeps it out of the
layout until it is restored; `tile retile` does not undo that choice.

Maximizing a window with the square title-bar button auto-hides docked bars on its monitor and
releases their work area. Move the pointer to the bar's screen edge to reveal it temporarily.
Restore the window, or focus it and run `wakuc tile toggle` (`Alt+F` in the sample bindings), to
restore it into tiling and bring the bar back. The explicit `tile float` command leaves a
maximized window floating.

Native fullscreen, such as Edge's `F11`, also stays outside the layout while the bar releases
its work area. Exit native fullscreen with the application's own shortcut to return to tiling.

In `mainStack`, drag a window border or corner to resize its tile. Neighbors follow shared-divider
changes during the drag. Outer edges snap back into the work area on release. Dragging the title
bar still floats the window; other layouts retain their move/resize-to-float behavior.

Most actions operate on the foreground window and its workspace:

- `toggle`: switch the focused window between floating and tiled.
- `float`: make the focused window float.
- `layout <name>`: set the workspace layout. Names are `mainStack`, `mainStackTop`, `columns`,
  `rows`, `bsp`, `grid` and `centerMain`. Use `next` or `previous` to cycle through them.
- `retile`: arrange windows again.
- `status`: report tiler state and workspace details.

Further commands:

```text
wakuc tile focus next|previous|left|right|up|down
wakuc tile move left|right|up|down
wakuc tile promote
wakuc tile stack left|right|up|down
wakuc tile unstack
wakuc tile cycle-stack next|previous
wakuc tile resize horizontal|vertical increase|decrease
wakuc tile flip horizontal|vertical
wakuc tile monocle
wakuc tile pause
wakuc tile close
wakuc tile minimize
```

- `focus` selects another tiled slot. Directional focus does not wrap.
- `move` swaps the focused slot with its directional neighbour; `promote` moves it to the first
  slot.
- `stack` joins the focused window to the neighbouring slot. `unstack` gives it a separate slot.
  `cycle-stack` selects the next or previous window in its stack.
- `resize` changes the layout's horizontal split or, where supported, a stack window's vertical
  weight. Not every layout supports resizing.
- `flip` mirrors the layout. `monocle` toggles a full-work-area slot. `pause` pauses or resumes
  placement.
- `close` requests that the foreground window close; it may prompt to save. `minimize` minimizes
  it. These also work on eligible floating windows.

See the [tiling design](https://github.com/JanDeDobbeleer/waku/blob/main/src/features/tiling/design.md)
for layouts, stacking and limitations.

## Call a shell function

```powershell
wakuc call
wakuc call <target> <function> [arguments...]
```

`call` lists enabled `IpcHandler` targets and their functions. Supply a target and function to
invoke one; available functions depend on the running shell. For example, if the shell exposes a
target `launcher` with a function `open`, run:

```powershell
wakuc call launcher open
```

Arguments are separate words; quote a value containing spaces. Functions accept at most ten
arguments. Untyped arguments arrive as strings; typed arguments support strings, integers,
booleans, real numbers and colors. The result is printed on one line; void or null returns print
`ok`. Lists and objects are printed as compact JSON.

## Exit codes and troubleshooting

- `0`: success.
- `1`: the bar returned an error, or a local config or startup operation failed.
- `2`: no bar is running for the selected config home.
- `3`: invalid usage or a communication failure, including no answer.

Errors go to standard error. If a command cannot find the bar, check that waku is running and
that `wakuc` uses the same config home. Shell-specific shortcuts, surfaces and IPC handlers exist
only when the running shell defines them. Wallpaper, desktop and tiling commands can report that
their service or requested operation is unavailable.

## Related documentation

- [Installation and sign-in startup](Installation)
- [Shell author notes](https://github.com/JanDeDobbeleer/waku/blob/main/docs/shells.md)
- [waku flags and building](https://github.com/JanDeDobbeleer/waku/blob/main/docs/building.md)
- [wakukeys hotkey guide](wakukeys)
- [Application overview](https://github.com/JanDeDobbeleer/waku/blob/main/src/app/README.md)
