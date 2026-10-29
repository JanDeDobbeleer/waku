# wabar

Pronounced /wab-bar/: "Windows, actually. With a bar."

wabar is a native C++20/Qt 6.10 Windows bar, a port of [YASB](https://github.com/amnweb/yasb), scoped to the widgets in one config.
The Python build (`D:\yasb`, branch `local/build`) is the behavioral reference.

## Documentation

| Document | What it covers |
| --- | --- |
| [AGENTS.md](AGENTS.md) | Rules for contributors and agents: commands, structure rules, adding a feature, style, commits |
| [docs/building.md](docs/building.md) | Prerequisites, presets, deploy, command-line flags, `tools\check.ps1`, hooks |
| [docs/architecture.md](docs/architecture.md) | Units and layering, threads, singletons, error policy, undocumented-API allowlist |
| [docs/shells.md](docs/shells.md) | Notes for shell authors: what differs from Quickshell (input masks, shortcuts) |
| [src/app/README.md](src/app/README.md) | Running `wabar` and `wabarc`, config home, QML modules |
| `src/features/<f>/design.md` | The design note of each feature |
| [docs/plans](docs/plans/README.md) | Dated design notes and plans, with their status |
| [docs/archive](docs/archive/README.md) | Records of the retired Qt Widgets build |
| [SECURITY.md](SECURITY.md) | Threat model and how to report a problem |

## Licence and credits

wabar is released under the MIT licence ([LICENSE](LICENSE)). It began as a C++ port of
[YASB](https://github.com/amnweb/yasb) and still uses what YASB found out about Windows (tray
messages, window tracking, notifications). [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) holds the
notices that must travel with any copy.
