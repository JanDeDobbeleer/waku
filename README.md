# wabar

Pronounced /wab-bar/: "Windows, actually. With a bar."

wabar is a native C++20/Qt 6.10 Windows bar, a port of [YASB](https://github.com/amnweb/yasb), scoped to the widgets in one config.
The Python build (`D:\yasb`, branch `local/build`) is the behavioral reference.

## Features

- Hides the native Windows taskbar while it runs and restores it at exit (`hide_windows_taskbar`,
  `--keep-taskbar`; see [docs/taskbar-hiding.md](docs/taskbar-hiding.md)).

## Documentation

| Document | What it covers |
| --- | --- |
| [AGENTS.md](AGENTS.md) | Rules for agents and contributors: commands, layering, style, commits, definition of done |
| [docs/building.md](docs/building.md) | Prerequisites, build/test commands, CLI, `tools\check.ps1` |
| [docs/taskbar-hiding.md](docs/taskbar-hiding.md) | Hiding the native taskbar: mechanism, work area, crash handling |
| [docs/architecture.md](docs/architecture.md) | Layering, ownership and threading, singletons, error policy, undocumented-API allowlist |
| [tools/parity/README.md](tools/parity/README.md) | Exporting parity vectors from Python YASB |
| [docs/parity-checklist.md](docs/parity-checklist.md) | Per-widget parity status |
| [docs/baseline.md](docs/baseline.md) | Python vs native resource baseline and A/B numbers |
| [docs/plans/2026-10-27-001-feat-cpp-qt-port-plan.md](docs/plans/2026-10-27-001-feat-cpp-qt-port-plan.md) | Port plan (architectural record) |
| [docs/plans/2026-10-27-002-refactor-design-review-remediation-plan.md](docs/plans/2026-10-27-002-refactor-design-review-remediation-plan.md) | Design review remediation plan |
| [DESIGN_REVIEW.md](DESIGN_REVIEW.md) | Design review findings |
| [SECURITY.md](SECURITY.md) | Threat model |

## Licence and credits

wabar is released under the MIT licence ([LICENSE](LICENSE)). It began as a C++ port of
[YASB](https://github.com/amnweb/yasb) and still uses what YASB found out about Windows (tray
messages, window tracking, notifications). [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) holds the
notices that must travel with any copy.
