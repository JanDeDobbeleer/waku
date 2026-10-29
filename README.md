# yasb-native

A native C++20/Qt 6.10 port of [YASB](https://github.com/amnweb/yasb), scoped to the widgets in one config.
The Python build (`D:\yasb`, branch `local/build`) is the behavioral reference.

The Qt Quick bar in `spike/` is called wabar (/ˈwæbɑːr/, "wab-bar": "Windows, actually. With a bar."); see
[spike/README.md](spike/README.md).

## Documentation

| Document | What it covers |
| --- | --- |
| [AGENTS.md](AGENTS.md) | Rules for agents and contributors: commands, layering, style, commits, definition of done |
| [docs/building.md](docs/building.md) | Prerequisites, build/test commands, CLI, `tools\check.ps1` |
| [docs/architecture.md](docs/architecture.md) | Layering, ownership and threading, singletons, error policy, undocumented-API allowlist |
| [tools/parity/README.md](tools/parity/README.md) | Exporting parity vectors from Python YASB |
| [docs/parity-checklist.md](docs/parity-checklist.md) | Per-widget parity status |
| [docs/baseline.md](docs/baseline.md) | Python vs native resource baseline and A/B numbers |
| [docs/plans/2026-10-27-001-feat-cpp-qt-port-plan.md](docs/plans/2026-10-27-001-feat-cpp-qt-port-plan.md) | Port plan (architectural record) |
| [docs/plans/2026-10-27-002-refactor-design-review-remediation-plan.md](docs/plans/2026-10-27-002-refactor-design-review-remediation-plan.md) | Design review remediation plan |
| [DESIGN_REVIEW.md](DESIGN_REVIEW.md) | Design review findings |
| [SECURITY.md](SECURITY.md) | Threat model |

## Licence

MIT, see [LICENSE](LICENSE). Derived from YASB and qt-css-engine (both MIT); see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
