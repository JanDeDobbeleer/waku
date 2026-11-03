# Plans

Dated design notes and plans. They record how decisions were made; they are not kept up to date.
Paths, branch names and targets in them are those of their day. The living description of the code
is `docs/architecture.md`, and each feature has its own `design.md` under `src/features/<f>/`.

| Plan | What it is | Status |
| --- | --- | --- |
| [2026-10-28-qt-quick-bar-config-design.md](2026-10-28-qt-quick-bar-config-design.md) | QML as the config format: `bar.qml`, `theme.qml`, `widgets/`, hot reload, popups | Implemented. The sample config in `examples/config` follows it. Read it for the reasons behind the config format; its paths (`spike/`) are historical. |
| [2026-10-29-wabar-architecture-proposal.md](2026-10-29-wabar-architecture-proposal.md) | Vertical slices: `src/{kernel,control,host,ui,features,app}`, `wabar_feature()`, rules S1-S8, migration steps 0-11 | Implemented. Section 11 holds the owner's decisions. `docs/architecture.md` and `AGENTS.md` now carry the rules. |
| [2026-10-29-wabar-extensibility-proposal.md](2026-10-29-wabar-extensibility-proposal.md) | User-defined surfaces on every edge, launchers and overlays; section 11 adopts Quickshell's vocabulary (the names `wabar` 2.0 uses) | Implemented in large part. Section 11 wins where it conflicts. Open: steps 10, 11, 14 and 15 (the 2026-10-30 handoff, section 4 item 5). Its status line is out of date. |
| [2026-10-29-wabar-styling-proposal.md](2026-10-29-wabar-styling-proposal.md) | Why a look change needs a rebuild and how to stop that: rich theme tokens, style bundles, loading the built-ins from disk | Proposal. Not implemented. Sections 1 and 2 still describe the code. Section 6 holds the owner's open questions. |
| [2026-10-31-workarea-sync.md](2026-10-31-workarea-sync.md) | Keeping komorebi's cached work area in sync with wabar's app bar from the engine instead of the port's `pwsh` workaround: evidence, options A-E, the recommended guard in `workspaces`, work breakdown, live checks | Implemented (option C, commits 1, 2 and 4; not commit 3). Open: the live check of section 6. |
| [2026-11-01-handoff.md](2026-11-01-handoff.md) | Current state on `main`, the live setup, how to work, open items and a map (read this first to continue) | Current handoff. |
