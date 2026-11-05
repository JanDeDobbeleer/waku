# waku

waku (枠, "frame", say "wah-koo") is a Qt Quick top bar for Windows, configured in QML. The name is
the Japanese word for a window frame. It began as a counterpart of Waybar, the bar for Wayland. It is
written in lower case and plain ASCII in code and paths.

- `waku.exe` runs the bar; `wakukeys.exe` is the hotkey daemon (below); `wakuc.exe` controls a running bar:
  - `wakuc reload`, `wakuc status`, `wakuc quit`
  - `wakuc config init [--force]`: write a starter `shell.qml` (the built-in bar) into the config
    home so you can edit it. It needs no running bar. It refuses, with exit code 1, to replace an
    existing `shell.qml` unless you pass `--force`; it touches no other file
  - `wakuc startup enable|disable|status`: register, remove or report the logon Scheduled Task that
    starts `waku.exe`, and the keys daemon `wakukeys.exe` when `keys.conf` exists (no running bar
    needed). `waku.exe` must be on your PATH. `enable` takes
    `--config-home <dir>`, `--no-app-bar`, `--keep-taskbar` and extra `waku` flags after `--`
  - `wakuc wallpaper set <file-or-path>`: a full path to any image, or a file name in the
    wallpaper folder (case does not matter, the extension may be left out; an ambiguous name is an
    error that lists the candidates)
  - `wakuc wallpaper random`, `next`, `previous`: pick from the wallpaper folder (they wrap)
  - `wakuc wallpaper gallery`: open or close the picker
  - `wakuc call <target> <function> [args...]`: run a function of an `IpcHandler` declared in the
    shell; `wakuc call` alone lists the targets
  - `wakuc shortcut <name>`: fire the `GlobalShortcut` with that name; `wakuc shortcut list`
    prints the names
  - `wakuc surface list`, `wakuc surface show|hide|toggle|status <name>`: the shell windows that
    have a `name` (a launcher, an OSD); `show` and `toggle` let the bar take the keyboard focus
  - `--dry-run` after `wallpaper` (`set`, `random`, `next`, `previous`) plays the animation but does
    not set the wallpaper

  Exit code: 0 ok, 1 the bar answered an error, 2 no bar is running, 3 usage error. The wallpaper
  folder, hotkey and animation come from a `WallpaperSettings { ... }` next to the windows in `shell.qml`
  (`import waku.services as S`, then `S.WallpaperSettings`: the services share names with the
  widgets, so import them under an alias) or from a `Wallpapers` widget.
- `wakukeys.exe` is a hotkey daemon, a separate Windows (GUI) program with no console window:
  `wakukeys [--config <file>] [--config-home <dir>] [--log-file <file>]` loads a whkd binding file and
  runs the keys (one daemon per user); `wakukeys reload|stop|status` talk to it and print to the
  terminal (exit code 0 ok, 1 error, 2 not running, 3 usage). See `docs/building.md` and
  `src/features/keymap/design.md`.
- Config home: a folder with `shell.qml`, optionally `theme.qml` and your own widgets in `widgets/`.
  Select it with `--config-home <dir>` or `WAKU_CONFIG_HOME`; without either, `waku` and `wakuc`
  use `%USERPROFILE%\.config\waku` (also when it does not exist, so both agree on the control
  pipe). While that implicit folder has no `shell.qml`, `waku` shows the built-in bar
  (`DefaultShell.qml`) with the default theme and switches to `shell.qml` when it appears. A chosen
  config home without `shell.qml` is an error. Copy `examples/config` to start a config.
  Edit and save: the shell reloads. A 1.x `bar.qml` without a `shell.qml` shows an error that
  names the rename (`docs/building.md`).
- QML modules: `waku` 2.0 (ShellRoot, Scope, PanelWindow, FloatingWindow, Panel, PanelLayout,
  Variants, LazyLoader, Theme, ...), `waku.widgets` (the built-in widgets), `waku.services` (the C++
  data sources), and the Quickshell compatibility modules (`Quickshell`, `Quickshell.Wayland`,
  `Quickshell.Io` and others: the same types under Quickshell's names, for ported shells; the list
  is in `docs/architecture.md`).
- Panels dock to any of the four edges (`examples/sidebar` is a left one); a docked panel
  reports its edge as `QsWindow.window.dockEdge`.
- `--hide-taskbar` hides the Windows taskbar while waku runs and restores it on exit;
  `wakuc taskbar hide|show|status` does the same at run time.
- `--register-app-bar` reserves screen work area; `--no-tray` never hooks Explorer; `--safe` starts
  the built-in shell and ignores the config; `--font-engine=directwrite` honours variable-font axes.
- `Tiling.enabled = true` (a shell property) turns on a built-in tiler that places new windows in
  a main+stack or columns layout per monitor and virtual desktop; off by default,
  `--no-tiling` forbids it; `wakuc tile toggle|float|layout|retile|status`.
- `--log-file <path>` writes a JSON Lines diagnostics log (log messages, screen and scale
  changes, reloads, start and stop); off by default. Format: `docs/logging.md`.

The architecture is in [docs/architecture.md](../../docs/architecture.md), the build and every flag
in [docs/building.md](../../docs/building.md), notes for shell authors in
[docs/shells.md](../../docs/shells.md), and dated plans in [docs/plans](../../docs/plans/README.md).
The sample config is in [examples/config](../../examples/config), a vertical sidebar in
[examples/sidebar](../../examples/sidebar).
