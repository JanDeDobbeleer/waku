# waku startup reference

`waku` runs the Windows shell. After [installation](Installation),
start it from the Start menu or run `waku` in a terminal. Use [wakuc](wakuc) to control the
running instance.

This reference describes the current development version. Your installed release may have
different options.

## Configuration

waku selects its config home in this order: `--config-home <dir>`, `WAKU_CONFIG_HOME`, then
`%USERPROFILE%\.config\waku`. The folder holds `shell.qml` and optionally `theme.qml`,
`widgets\` and `keys.conf`.

When the default config home has no `shell.qml`, waku starts its built-in bar. If you explicitly
select a config home that has no `shell.qml`, waku reports an error instead. Create a starter
configuration with `wakuc config init`, or use
[the sample configuration](https://github.com/JanDeDobbeleer/waku/tree/main/examples/config).
An instance is identified by its config home, so use the same path with `wakuc`.

## Options

| Option | Effect |
| --- | --- |
| `--config-home <dir>` | Select the config home. |
| `--safe` | Start the built-in shell and default theme, ignoring the config. |
| `--watch` | Reload when shell QML or `qmldir` files in the config home change. Otherwise use `wakuc reload`. |
| `--import-path <dir>` | Add a QML module folder. Repeat for multiple folders. |
| `--no-app-bar` | Do not reserve screen work area for panels. |
| `--keep-taskbar` | Leave the Windows taskbar alone at startup. |
| `--no-tray` | Do not claim the system tray; the Tray widget remains empty. |
| `--no-keys` | Do not automatically start `wakukeys` for `keys.conf`. |
| `--no-hotkeys` | Do not register `GlobalShortcut` keys. Named shortcuts remain available through `wakuc`. |
| `--no-tiling` | Do not start the built-in tiler. |
| `--no-shell-desktops` | Do not use the shell interface for virtual desktop operations; some operations become unavailable. |
| `--font-engine <engine>` | Choose `gdi` (default) or `directwrite` for variable-font axes. `QT_QPA_PLATFORM`, if set, takes precedence. |
| `--log-file <file>` | Write a JSON Lines diagnostics log. |
| `--help`, `-h` | Show the options. |

On a live desktop, waku normally reserves space for panels that request it, hides the Windows
taskbar and enables tiling. It restores its taskbar changes on exit. When `keys.conf` exists, waku
starts `wakukeys` and stops it on exit; an already-running daemon is left alone.

## Useful examples

Start with the built-in shell to diagnose a broken configuration:

```powershell
waku --safe
```

Try a separate shell without changing your normal config or taking over desktop features:

```powershell
waku --config-home 'C:\Temp\waku-test' --no-app-bar --keep-taskbar --no-tray --no-keys --no-tiling
wakuc --config-home 'C:\Temp\waku-test' status
wakuc --config-home 'C:\Temp\waku-test' quit
```

The selected folder must contain `shell.qml`. Capture diagnostics with
`--log-file 'C:\Temp\waku.log'`; see the
[logging guide](https://github.com/JanDeDobbeleer/waku/blob/main/docs/logging.md).

## Related documentation

- [Installation, updates and troubleshooting](Installation)
- [Shell author notes](https://github.com/JanDeDobbeleer/waku/blob/main/docs/shells.md)
- [wakuc command reference](wakuc)
- [wakukeys hotkey guide](wakukeys)
