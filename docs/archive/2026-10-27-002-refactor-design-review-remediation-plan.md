# Design review remediation plan

Date: 2026-10-27
Input: `DESIGN_REVIEW.md` (repo root), plus findings from the U8 live A/B run.
Goal: put the rails in place and fix the structural issues **before** porting the remaining 12 widgets.

## 1. Triage of the review

I checked the review's claims against the code (`app/main.cpp:81` static engine, `core/config/loader.cpp:183-185`
const `operator[]`, `yasb_core` single target, no `LICENSE`, no `clang-format` on the machine). They hold. Four
corrections change the plan:

| Review assumption | Reality | Effect on plan |
|---|---|---|
| Repo is (or will soon be) public on GitHub with hosted runners | `git remote -v` is empty; the repo is local only | CI hardening (SEC-3, CI-1..12, CODEOWNERS, Dependabot, branch protection) is **deferred** until a remote exists. Local enforcement (`tools/check.ps1` + git hooks) carries the load now. |
| Only qt-css-engine needs an MIT notice | The whole port derives from amnweb/yasb (MIT, the YASB `LICENSE`) | `THIRD_PARTY_NOTICES.md` must include the YASB notice as well as qt-css-engine. |
| Agents are Claude Code (`CLAUDE.md`, `.claude/settings.json` hooks) | Agents run in GitHub Copilot CLI | Use `AGENTS.md` + `.github/copilot-instructions.md` and git hooks (`core.hooksPath tools/hooks`) instead of Claude hooks. |
| Runtime findings are complete | A/B run found Qt defaulting to DirectWrite, mapping all 1,067 fonts (2,105 handles, 206 MiB WS) | Already fixed in `app/main.cpp` (`setFontEngine`, mirrors `src/env.py`). Commit it and add a rule: every Python env/startup side effect must be ported. |

## 2. What we adopt, defer or drop

**Adopt now (blocks further widget work):** SEC-1, 3.5 style, 3.1 layering, 3.2 composition root, 3.3 typed options,
3.4 error policy, RS-1, RS-2, RS-3, SEC-2, RS-7, RS-4, RS-5, RS-8, RS-9, RS-10, T-2, T-4, 7.1 repo files (adapted),
`tools/check.ps1`, generated-file drift check, `CMAKE_EXPORT_COMPILE_COMMANDS`, MSVC-only guard, drop unused WIL.

**Adopt, low priority (fold into the units below when touching the file):** SEC-5 `SendInput`, RS-6 semaphore,
RS-11 screen-event debounce, RS-12 `<windows.h>` out of public headers, SEC-4 undocumented-API allowlist,
docs fixes in section 8.

**Check parity before changing:** SEC-6 log rotation. Python YASB's logging behaviour is the reference; match it,
don't invent a new policy.

**Defer until a GitHub remote exists:** SEC-3, CI-1..CI-12, CODEOWNERS, PR template, Dependabot, CodeQL,
branch protection, conventional-commit check on PR titles.

**Defer, nice to have:** T-3 fuzzing, T-5 coverage, ASan job (x64 only; ARM64 ASan support unverified),
`/Qspectre` (needs the Spectre-mitigated libs installed for ARM64; verify before adding).

**Drop:** Claude Code hooks (replaced by git hooks), "public repo for ARM64 runners" rationale.

## 3. Work units

Each unit is one commit (conventional subject), green on `tools/check.ps1` before it lands.

### R0 — Commit pending work (orchestrator, serial)
- Commit the font-engine fix (`fix(app): default Qt font engine to GDI like Python YASB`).
- Commit the A/B harness and baseline edits separately (`chore(ab): ...`). Record the U8 A/B numbers in
  `docs/baseline.md` (private 19.6 MiB, WS 51.9 MiB, 426 handles after the fix; CPU to re-measure).
- Done when: `git status` clean, 179 tests green.

### R1 — Legal and agent rails (docs/chore)
- `LICENSE` (MIT, matching upstream), `THIRD_PARTY_NOTICES.md` (amnweb/yasb MIT, qt-css-engine MIT with commit
  `fadd1a992`), `SECURITY.md` (threat model from review 4.1).
- `AGENTS.md`: build/test commands, layering table, style decisions, error policy, "vectors and
  `core/config/options/*.h` are generated", "never run without `--config-home`; `--register-app-bar` only in
  `tools/ab`", undocumented-API rule, "port every Python startup side effect (env vars, Qt attributes)",
  conventional commit scopes, definition of done. `.github/copilot-instructions.md` points to it.
- `.editorconfig` as in review 7.1, keeping CRLF policy consistent with what the repo already uses.
- `docs/architecture.md`: layering, ownership/threading, singleton table, error policy, undocumented-API allowlist.
- Fix `docs/building.md` (`registerStatWidgets` is now called) and add a doc map to `README.md`.

### R2 — Local enforcement (chore)
- Install LLVM (clang-format, clang-tidy) via winget — **needs your approval** (system install).
- `tools/check.ps1 [-Fast]`: clang-format dry run, `generate_options.py` + `git diff --exit-code`,
  build + `ctest --output-on-failure --timeout 120` on `arm64-debug`. `-Fast` = format + generated only.
- `tools/hooks/pre-commit` (runs `check.ps1 -Fast`), `tools/hooks/commit-msg` (conventional regex);
  enable with `git config core.hooksPath tools/hooks` (documented in `AGENTS.md`).
- Presets: `CMAKE_EXPORT_COMPILE_COMMANDS ON`; `if(NOT MSVC) FATAL_ERROR`; `/external:anglebrackets /external:W0`.

### R3 — One-shot style normalisation (style, serial, touches everything)
- `generate_options.py` emits two-space indent; regenerate.
- Rename `m_` members to `_` suffix; root all includes (`"core/widgets/x.h"`); one Qt literal style
  (`QStringLiteral`); remove local `NOMINMAX` redefinitions.
- `clang-format -i` over `app core platform widgets tests tools/yasbc-native`.
- Must land before R4–R9 start, or every parallel branch conflicts.

### R4 — Runtime safety (fix) — `app/`, `core/config/loader.cpp`, `core/bar/bar_window.cpp`
- RS-1: engine owned by the composition root (see R6), destroyed before `QApplication`.
- RS-2: replace const `json::operator[]` with `find`/`at`/`value`; add a grep check to `check.ps1`.
- RS-3: null `primaryScreen()` / `screenByName` handled with a log line and distinct exit code.
- 3.4: top-level exception guard in `main`; `try`/log at every Qt→our-code entry that can throw
  (`label_format` `std::stoi`, generated `readField`).
- RS-6, RS-11 while in `app/`.

### R5 — IPC hardening (fix) — `app/cli_server.cpp`, log pipe
- SEC-2: SDDL restricted to the current user SID, `PIPE_REJECT_REMOTE_CLIENTS`.
- RS-7: handle `ERROR_MORE_DATA` (reject oversize explicitly).
- Verify `yasbc-native` still round-trips (`test_cli_protocol`); add a test that a second user/remote flag is rejected
  where feasible.

### R6 — Composition root (refactor) — `app/main.cpp`, `core/widgets/*`, `widgets/*`
- `Runtime` struct in `app/` owning `TransitionEngine`, `WidgetRegistry`, `EventBus`, `GlobalState`,
  `AppServices`; passed down via `WidgetContext`/`BarDeps`.
- Remove `instance()` singletons where the seam exists; keep `SchemaValidator::embedded()` and logging globals.
- RS-9: key `PollingWorker` by interval (or log the ignored interval). RS-10 goes away with the registry singleton.
- RS-8: move the PDH swap query into `WindowsMemoryDataSource`.

### R7 — Layered CMake targets (build)
- Split `yasb_core` into `yasb_platform`, `yasb_config`, `yasb_css`, `yasb_ui`, `yasb_widgets`, `yasb_app` per
  review 3.1, with `PRIVATE`/`PUBLIC` links encoding app → widgets → ui → css/config → platform.
- Move `widgets/*` out of `core/widgets/CMakeLists.txt`. Drop unused `WIL::WIL` (or adopt it in R5 for handles —
  decide in R5). Move WinRT libs to the target that uses them.
- Done when: an include from `core/` into `widgets/` fails to compile/link.

### R8 — Typed options (refactor) — `widgets/{cpu,memory,battery}`, `core/bar/bar_window.cpp`
- Widgets receive `const options::XConfig&` through `WidgetContext`; delete restated defaults
  (`cpu_widget.cpp:41,59,62`, `system_widget_helpers.h:42-59`, `bar_window.cpp:47-86`).
- Template for the 12 remaining widgets; record the rule in `AGENTS.md`.

### R9 — Performance hot paths (perf) — `core/css/transition_engine.cpp`, `loader.cpp`, `label_format.cpp`
- RS-4: replace `std::regex` with hand scanners or `QRegularExpression`.
- RS-5: static regex, cached per-widget cascade invalidated on property/parent/stylesheet change.
- Add a micro-benchmark test (e.g. 200 widgets × N events) with a generous budget so regressions are visible.

### R10 — Test infrastructure (test)
- T-4: shared `test_main.cpp` with one gtest `Environment` owning `QApplication`.
- T-2: replace sleeps with event waits/deadlines in `test_single_instance`, `test_cli_protocol`.
- T-1: tests for `AppServices` (with injected collaborators from R6), `WidgetBuilder`, `log.cpp`.
- T-6: wabar `tests/CMakeLists.txt`.

### R11 — Static analysis (ci, local)
- `.clang-tidy` per review 7.1 (members `lower_case_` suffix). MSVC `/analyze` configure option.
- Add to `check.ps1` as a separate `-Analyze` switch; fix the baseline findings.

### R12 — Re-validate (orchestrator)
- Full build (ARM64 Debug/Release, x64 Release), all tests, offscreen screenshot.
- Re-run the live A/B (Python vs native, three-widget config, same procedure as the U8 run) and compare against
  the 19.6 MiB / 51.9 MiB / 426-handle result. No regression allowed; visual diff must still match.

## 4. Order and parallelism

```
R0 ─► R1 ─► R2 ─► R3 ─┬─► Track A: R4 ─► R5 ─► R6 ─► R10
                      ├─► Track B: R7 ─► R8
                      └─► Track C: R9
                                      all ─► R11 ─► R12
```

- R0–R3 are serial: R3 rewrites every file, so nothing runs in parallel with it.
- After R3, three worktree tracks run in parallel (as with U7/U8). Track A owns `app/`; Track B owns CMake and
  `widgets/`; Track C owns `core/css`, `loader.cpp`, `label_format.cpp`. R6 touches `WidgetContext`, which R8
  also uses: B rebases on A's R6 before R8, or R8 waits for R6.
- Model use as before: a high-capability model for R6/R7 (design-sensitive), the fast model for R1, R3, R10, R11.

Rough effort with three parallel tracks: R0–R3 about half a day of agent time; tracks A/B/C about a day; R11–R12
about half a day including the 15-minute live A/B.

## 5. Decisions needed before starting

1. Licence for yasb-native: MIT (same as upstream) recommended.
2. Install LLVM (clang-format/clang-tidy) system-wide via winget: needed for R2/R3/R11.
3. Member naming: `_` suffix (majority, matches `.clang-format`) recommended.
4. Line endings: keep current repo policy (check before R1 writes `.editorconfig`).
5. Push to a GitHub remote now (enables the deferred CI items) or stay local.

## 6. Out of scope

Porting the remaining 12 widgets, manual popup sign-off for U8, and the 30-minute log comparison. They resume
after R12.
