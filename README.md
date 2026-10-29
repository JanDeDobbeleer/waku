# wabar

wabar (/ˈwæbɑːr/, "wab-bar": "Windows, actually. With a bar.") is a Qt Quick top bar for Windows
(C++20, Qt 6.10), configured in QML. It started as a native port of [YASB](https://github.com/amnweb/yasb);
the retired Qt Widgets build is at the git tag `yasb-native-final` and described in
[docs/archive](docs/archive/README.md). See [src/app/README.md](src/app/README.md) for how to run and
configure it; a sample config is in [examples/config](examples/config).

## Documentation

| Document | What it covers |
| --- | --- |
| [AGENTS.md](AGENTS.md) | Rules for agents and contributors: commands, layering, style, commits, definition of done |
| [docs/building.md](docs/building.md) | Prerequisites, build/test commands, `tools\check.ps1` |
| [docs/architecture.md](docs/architecture.md) | Layering, threading, singletons, error policy, undocumented-API allowlist |
| [docs/plans](docs/plans) | Design notes and plans (architecture proposal, widgets) |
| [docs/archive](docs/archive/README.md) | Records of the retired Widgets build |
| [SECURITY.md](SECURITY.md) | Threat model |

## Licence

MIT, see [LICENSE](LICENSE). Derived from YASB (MIT); see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
