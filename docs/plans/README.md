# Plans

Dated design notes and plans. They record how decisions were made; they are not kept up to date.
Paths, branch names and targets in them are those of their day. The living description of the code
is `docs/architecture.md`, and each feature has its own `design.md` under `src/features/<f>/`.

| Plan | What it is | Status |
| --- | --- | --- |
| [2026-10-28-qt-quick-bar-config-design.md](2026-10-28-qt-quick-bar-config-design.md) | QML as the config format: `bar.qml`, `theme.qml`, `widgets/`, hot reload, popups | Implemented. The sample config in `examples/config` follows it. Read it for the reasons behind the config format; its paths (`spike/`) are historical. |
| [2026-10-29-wabar-architecture-proposal.md](2026-10-29-wabar-architecture-proposal.md) | Vertical slices: `src/{kernel,control,host,ui,features,app}`, `wabar_feature()`, rules S1-S8, migration steps 0-11 | Implemented. Section 11 holds the owner's decisions. `docs/architecture.md` and `AGENTS.md` now carry the rules. |

Earlier plans for the retired Qt Widgets build are in [../archive](../archive/README.md); they are
superseded by the plans above.
