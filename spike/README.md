# wabar

wabar (/ˈwæbɑːr/, say "wab-bar": the a is the one in "actually"; "Windows, actuallee") is a Qt Quick
top bar for Windows, configured in QML. The name stands for "Windows, actually. With a bar." and nods
to Waybar, the bar for Wayland. It is written in lower case and plain ASCII in code and paths.

- `wabar.exe` runs the bar; `wabarc.exe` controls a running one (`wabarc reload`, `status`, `quit`).
- Config home: `%USERPROFILE%\.config\wabar\` (`bar.qml`, `theme.qml`, your own widgets), or
  `--config-home <dir>` / `WABAR_CONFIG_HOME`. Edit and save: the bars reload.
- QML modules: `wabar` (Config, Bar, BarLayout, Theme), `wabar.widgets` (the built-in widgets) and
  `wabar.services` (the C++ data sources).
- `--register-app-bar` reserves screen work area; `--no-tray` never hooks Explorer; `--safe` starts
  the built-in bar and ignores the config.

Design notes and plans are in [docs/plans](../docs/plans). The sample config is in
[spike/config](config).
