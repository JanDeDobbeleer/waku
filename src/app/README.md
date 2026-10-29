# wabar

wabar (/ˈwæbɑːr/, say "wab-bar": the a is the one in "actually"; "Windows, actuallee") is a Qt Quick
top bar for Windows, configured in QML. The name stands for "Windows, actually. With a bar." and nods
to Waybar, the bar for Wayland. It is written in lower case and plain ASCII in code and paths.

- `wabar.exe` runs the bar; `wabarc.exe` controls a running one:
  - `wabarc reload`, `wabarc status`, `wabarc quit`
  - `wabarc wallpaper set <file-or-path>`: a full path to any image, or a file name in the
    wallpaper folder (case does not matter, the extension may be left out; an ambiguous name is an
    error that lists the candidates)
  - `wabarc wallpaper random`, `next`, `previous`: pick from the wallpaper folder (they wrap)
  - `wabarc wallpaper gallery`: open or close the picker
  - `--dry-run` after `wallpaper` (`set`, `random`, `next`, `previous`) plays the animation but does
    not set the wallpaper

  Exit code: 0 ok, 1 the bar answered an error, 2 no bar is running, 3 usage error. The wallpaper
  folder, hotkey and animation come from a `WallpaperSettings { ... }` next to the bars in `bar.qml`
  (`import wabar.services as S`, then `S.WallpaperSettings`: the services share names with the
  widgets, so import them under an alias) or from a `Wallpapers` widget.
- Config home: a folder with `bar.qml`, optionally `theme.qml` and your own widgets in `widgets/`.
  Select it with `--config-home <dir>` or `WABAR_CONFIG_HOME`; without either, `wabar` and `wabarc`
  use the `examples/config` of the checkout they were built from. A personal config
  conventionally lives in `%USERPROFILE%\.config\wabar`; pass that folder with `--config-home`.
  Edit and save: the bars reload.
- QML modules: `wabar` (Config, Bar, BarLayout, Theme), `wabar.widgets` (the built-in widgets) and
  `wabar.services` (the C++ data sources).
- `--register-app-bar` reserves screen work area; `--no-tray` never hooks Explorer; `--safe` starts
  the built-in bar and ignores the config.

The architecture is in [docs/architecture.md](../../docs/architecture.md), the build and every flag
in [docs/building.md](../../docs/building.md), and dated plans in [docs/plans](../../docs/plans/README.md).
The sample config is in [examples/config](../../examples/config).
