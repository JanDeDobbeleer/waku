# waku

waku (枠, "frame", say "wah-koo") is a top bar for Windows, written in C++20 and Qt 6.10
(Qt Quick) and configured in QML. The name is the Japanese word for the frame around a window
(Dutch: kozijn), and the frame of your desktop is where the bar lives. The project began as a
Windows counterpart of [Waybar](https://github.com/Alexays/Waybar), the bar for Wayland.

The bar is a set of QML files. `shell.qml` says which bars to show and which widgets go where;
`theme.qml` sets the look. Save either file and the bars reload. Built-in widgets cover the clock,
workspaces (komorebi and GlazeWM), media, volume, Bluetooth, Wi-Fi, battery, CPU, memory, the tray,
the taskbar, notifications, GitHub, wallpapers and a control center. You can copy any of them into
your config and change it.

## Quick start

Build and test with the steps in [docs/building.md](docs/building.md), then run the bar with the
sample config:

```powershell
build\arm64-debug\waku.exe --config-home examples\config
build\arm64-debug\wakuc.exe --config-home examples\config status
```

`wakuc` talks to the running bar. Its commands are `reload`, `status`, `quit`,
`wallpaper set|random|next|previous|gallery`, `surface list|show|hide|toggle|status <name>`,
`call [<target> <function> [args...]]`, `shortcut <name>|list` and `taskbar hide|show|status`.
Details and flags: [src/app/README.md](src/app/README.md) and [docs/building.md](docs/building.md).

Use `--config-home <dir>` (or `WAKU_CONFIG_HOME`) to point waku at your own folder, and
`--no-tray` if you do not want it to hook Explorer. `cmake --build --preset arm64-debug --target
deploy` assembles a portable folder in `dist/waku`.

## Documentation

| Document | What it covers |
| --- | --- |
| [AGENTS.md](AGENTS.md) | Rules for contributors and agents: commands, structure rules, adding a feature, style, commits |
| [docs/building.md](docs/building.md) | Prerequisites, presets, deploy, command-line flags, `tools\check.ps1`, hooks |
| [docs/architecture.md](docs/architecture.md) | Units and layering, threads, singletons, error policy, undocumented-API allowlist |
| [docs/shells.md](docs/shells.md) | Notes for shell authors: what differs from Quickshell (input masks, shortcuts) |
| [src/app/README.md](src/app/README.md) | Running `waku` and `wakuc`, config home, QML modules |
| `src/features/<f>/design.md` | The design note of each feature |
| [docs/plans](docs/plans/README.md) | Dated design notes and plans, with their status |
| [docs/archive](docs/archive/README.md) | Records of the retired Qt Widgets build |
| [SECURITY.md](SECURITY.md) | Threat model and how to report a problem |

## Licence and credits

waku is released under the MIT licence ([LICENSE](LICENSE)). It began as a C++ port of
[YASB](https://github.com/amnweb/yasb) and still uses what YASB found out about Windows (tray
messages, window tracking, notifications). [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) holds the
notices that must travel with any copy.
