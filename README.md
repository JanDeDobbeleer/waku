<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/waku-logo-dark.svg">
    <img src="docs/assets/waku-logo.svg" alt="waku, the Windows shell bar" width="360">
  </picture>
</p>

# waku

waku (枠, "frame", say "wah-koo") is a top bar for Windows, written in C++20 and Qt 6.10
(Qt Quick) and configured in QML. The name is the Japanese word for the frame around a window
(Dutch: kozijn), and the frame of your desktop is where the bar lives.

Built-in widgets cover the clock, workspaces (komorebi and GlazeWM), media, volume, Bluetooth,
Wi-Fi, battery, CPU, memory, the tray, the taskbar, notifications, GitHub, wallpapers and a control
center. You can copy any of them into your config and change it.

## Install

Download the MSIX for your processor (`waku-x64.msix` or `waku-arm64.msix`) from the
[latest release](https://github.com/JanDeDobbeleer/waku/releases/latest) and open it.
Then start `waku` from the Start menu or a terminal.
The full guide is [docs/install.md](docs/install.md).

## Customise

waku reads its config from a folder, the config home: `--config-home <dir>`, then the
`WAKU_CONFIG_HOME` variable, then `%USERPROFILE%\.config\waku`. Without a `shell.qml` there, you get
a built-in bar.

`shell.qml` says which bars to show and which widgets go where; `theme.qml` sets the look. Save
either file and the bar reloads. Start from [examples/config](examples/config).

```qml
import waku
import waku.widgets

ShellRoot {
    Panel {
        edge: Panel.Top
        thickness: 40
        content: PanelLayout {
            start: [ Clock {} ]
            end: [ Volume {}, Battery {} ]
        }
    }
}
```

`wakuc` controls the running bar (`reload`, `status`, `quit` and more). See
[src/app/README.md](src/app/README.md) for its commands.

## Build from source

Install Visual Studio Build Tools, Qt 6.10 and vcpkg, then:

```powershell
cmake --preset arm64-release
cmake --build --preset arm64-release
```

Use `x64-release` on an x64 PC. Prerequisites, presets and the flags of `waku` are in
[docs/building.md](docs/building.md).

## Documentation

| Document | What it covers |
| --- | --- |
| [docs/install.md](docs/install.md) | Install from the MSIX, first run, updates, troubleshooting |
| [docs/shells.md](docs/shells.md) | Notes for shell authors: what differs from Quickshell |
| [docs/building.md](docs/building.md) | Prerequisites, presets, deploy, command-line flags |
| [docs/architecture.md](docs/architecture.md) | Units and layering, threads, singletons, error policy |
| [src/app/README.md](src/app/README.md) | Running `waku` and `wakuc`, config home, QML modules |
| [SECURITY.md](SECURITY.md) | Threat model and how to report a problem |

## Licence and credits

waku is released under the MIT licence ([LICENSE](LICENSE)). It began as a C++ port of
[YASB](https://github.com/amnweb/yasb) and still uses what YASB found out about Windows (tray
messages, window tracking, notifications). [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) holds the
notices that must travel with any copy.
