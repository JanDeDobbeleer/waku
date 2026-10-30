# Delivery plan: Quickshell-compatible shell engine and the caelestia port

Status: plan, 2026-10-30. Branch `perf/lazy-surfaces` at `729d222`.
Inputs: `2026-10-29-wabar-extensibility-proposal.md` (section 11 wins on names and on input regions)
and `2026-10-30-caelestia-port-assessment.md`. The owner answered open questions 12-15 with the
recommended "yes". This plan says who builds what, in which order, and how it is verified.

Goal of the whole run: a user ports caelestia (and shells like it) to Windows with wabar, and the
result is validated by porting caelestia and looking at it.

## 1. Roles and models

The orchestrator (this session) plans, briefs, merges and verifies. It never trusts an agent's
"done": it reads the diff, runs the full `ctest` and looks at the running result.

| Model | Used for | Why |
|---|---|---|
| Opus | step 1 (the breaking surface model), the `mask: Region` integration | wide blast radius, design judgment, one shot to get the API right |
| Sonnet | every standard step: services, `Process`/`FileView`, plugin path, edges and vertical layout, overlays, hotkeys, colour, example shells, the caelestia port workers | fast, cheap, strong enough when the brief is exact |
| Haiku | mechanical sweeps: docs sync, commit scopes, `AGENTS.md` and `architecture.md` rows, renames in examples | trivial and bounded |
| Fable | independent review at each gate (reads the diff, not the author's summary) and the final visual validation report | spend on judgment where a miss is expensive, not on typing |

Rules for every agent brief: read `AGENTS.md`; work in its own worktree and branch; touch only the
listed files; one logical commit per change with the attribution trailer; `tools\check.ps1` and a
full `ctest` green before reporting; no live-desktop action outside the rules in `AGENTS.md`
(`--config-home`, `--no-tray`, no clicks, no wallpaper change); report the commit hashes, what was
verified and what was not.

## 2. Dependency graph and parallelism

Lanes that do not touch `src/host/qml/` can start now. Step 1 owns `src/host/qml/` until it merges.

```
Wave 1 (now, parallel)
  A  step 1  surface model + Quickshell names        opus     src/host/, src/ui/, examples/   DONE
  B  spikes  S7 mask polling, S8 plugin load, S9 fonts sonnet  throwaway programs, no repo change
  C  item 3  Process shape, FileView, Quickshell      sonnet   src/features/shell/, kernel if needed

Lane A status (done, branch of lane A): the rename table is applied. `Config` -> `ShellRoot`
(+ `Scope`), `Bar` -> `Panel` (preset over `PanelWindow`), `BarLayout` -> `PanelLayout`,
`Window.window.spec` -> `QsWindow.window`, `anchorItem` -> `anchor.item`, `BarHost` ->
`ShellHost`, `bar.qml` -> `shell.qml`; `wabar` 2.0 plus the `Quickshell` and
`Quickshell.Wayland` modules. Merge point for lane C: the `Quickshell` singleton belongs in the
`Quickshell` module (`src/host/quickshell/`); its `screens` forwards to `wabar.ScreenList`
(`src/host/qml/shell_screen.h`, `ScreenList::of(engine)`), and `env`/`execDetached`/... come from
lane C's code. A QML singleton `Quickshell.qml` in that module that composes both avoids a
feature include from `host`.
Gate 1: Fable reviews A and C diffs; orchestrator merges, full ctest on the merge.

Wave 2 (parallel, after gate 1; spike results decide D)
  D  item 2  mask: Region (only if S7 passes)         opus     host mask, QML Region type
  E  steps 2-3  four edges, vertical layout, Strip    sonnet   host app bar, ui, widgets
  F  item 4+5  GlobalShortcut Hotkey, plugin path     sonnet   host hotkeys, app import path
  G  item 6  Material 3 colour                        sonnet   pure code and tests
  H  step 4  Overlay windows, focus, dismiss          sonnet   host overlay

Gate 2: Fable review; merge; full ctest.

Wave 3 (parallel)
  I  item 7  service gaps (Media.players, streams, Bluetooth, Desktop keys, idle)   sonnet, one agent per service
  J  item 8  layer: Background (WorkerW)              sonnet
  K  steps 9-11  Apps, desktop_windows, Clipboard     sonnet, one agent per feature
  L  docs sweep: architecture.md rows, AGENTS.md scopes, SECURITY.md, building.md   haiku

Gate 3: Fable review; merge; full ctest on arm64-debug, arm64-release and x64-release.

Wave 4: the validation port (section 4).
```

Merge order inside a wave: the lane with the fewest shared files first. Shared files are
`docs/architecture.md`, `src/features/CMakeLists.txt` and `tools/hooks/commit-msg`; they merge
line-locally and the orchestrator resolves conflicts. Concurrency is capped at three builds at once
(12 cores, a full build takes about 10 minutes).

## 3. Gates

A wave is done when all of these hold on the merged branch:

1. `tools\check.ps1` exits 0.
2. Full `ctest` passes (not a filtered subset).
3. A Fable review of the diff found no unresolved correctness or API-compatibility finding.
4. The orchestrator ran `wabar.exe` against `examples/` with `--config-home` and looked at it.
5. Each new Windows API call has its allowlist row in `docs/architecture.md`.

## 4. Validation: port caelestia and look at it

Done in wave 4 on a branch `port/caelestia`, in a config home outside the repository. Source:
a read-only clone of https://github.com/caelestia-dots/shell.

1. Fable lists the exact module set to port first, in this order: `StyledWindow` and the drawers
   shell, bar, launcher, OSD, dashboard (sans Linux-only data), sidebar, session menu, notifications
   list.
2. Sonnet workers port one module each, in parallel worktrees of the config home. Linux-only data
   sources are replaced by the wabar services or stubbed with a visible "unavailable" state.
3. The orchestrator runs the shell with `--config-home` and takes screenshots of each module open
   and closed, at 100% and at the user's display scale.
4. Fable judges each screenshot against caelestia's own screenshots in its README, and lists what
   differs and why (Windows limit, wabar gap, port bug).
5. Gaps found become issues in `docs/plans/` with a size, not silent workarounds.

Success: the bar, launcher, session menu and notification list render and respond; the drawers
open and close with their animations; input passes through the empty regions; every difference is
explained as a Windows limit or filed as a wabar gap.

## 5. Risks and the order that retires them

| Risk | Retired by |
|---|---|
| Cursor polling for input regions loses clicks or burns CPU | S7 in wave 1, before D is committed to |
| A Qt plugin built elsewhere will not load into `wabar.exe` | S8 in wave 1, before F is committed to |
| Variable-font axes need DirectWrite | S9 in wave 1 |
| Step 1 breaks the 614 tests | step 1 ports the tests in the same commit series; gate 1 |
| Parallel merges conflict | disjoint file lists in each brief; shared files merged by the orchestrator |
| A launched agent claims done with a failing build | the orchestrator reruns `check.ps1` and the full `ctest` |
