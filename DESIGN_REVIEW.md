# yasb-native design review

Date: 2026-10-27
Scope: every C++ source under `app/`, `core/`, `platform/`, `widgets/`, `tools/yasbc-native/`, the test suite, the CMake and vcpkg setup, the GitHub Actions workflow, and the docs. The uncommitted working-tree changes (main.cpp, baseline.md, the A/B scripts) were reviewed as part of the baseline. No code was changed.

Size at review time: about 9,400 lines of product C++ (excluding the generated option headers), 2,300 lines of tests (75 test cases, 5 of them parameterised over the parity vectors).

## 1. Verdict

The codebase is a sound foundation. The port is structured the way the plan says it should be, the parity-oracle approach turns "same behaviour as Python" into failing tests instead of opinions, and the hardening flags, the test seams and the pure-function split are better than most C++/Qt projects of this age. Agents can work in it.

What is missing is the enforcement layer. Nothing in the repository today stops an agent from committing unformatted code, breaking the intended layering, hand-editing generated or oracle files, or merging without a green build. Four things need to happen before autonomous work scales:

1. **Legal blocker.** There is no `LICENSE` file, and two source files declare themselves ports of MIT-licensed code (`core/css/rules.h:50`, `core/css/transition_engine.cpp:1-3`). MIT requires the notice to ship with the derived work. The plan still lists the qt-css-engine licence as an open question. Resolve it and add `LICENSE` plus `THIRD_PARTY_NOTICES.md`.
2. **Style is declared but not enforced.** `.clang-format` says two-space indent, yet `core/config/*` and the generated `options/*.h` use four. Member naming is split between `m_` prefix (6 headers) and `_` suffix (21 headers). Include paths are mixed between repo-rooted and file-relative. `clang-format` is not installed on the dev machine and not run in CI.
3. **Layering is intended but not expressed in the build.** Everything compiles into one static library, `yasb_core`, and `core/widgets/CMakeLists.txt` pulls the concrete widgets from `widgets/` into it. The dependency direction in the plan (app → widgets → core → platform) cannot be checked by the compiler because it does not exist as targets.
4. **CI only proves "it builds and the tests pass."** No format check, no static analysis, no sanitizer run, no generated-file drift check, no supply-chain pinning, no least-privilege token, no hooks for local enforcement.

Everything else in this document is detail under those four headings, plus a list of concrete runtime-safety fixes that are cheap now and expensive later.

## 2. What is good and should be preserved

- **Parity oracle.** `tools/parity` exports vectors from the Python build; C++ tests replay them (`tests/config/test_parity_config.cpp`, `tests/css/test_preprocessor.cpp`, `tests/bar/test_adaptive_frame.cpp`, `tests/widgets/test_label_format.cpp`). This is the single most valuable asset for autonomous agents: it gives them a ground truth they cannot argue with. Protect the vector files from hand edits (see section 7).
- **Test seams that keep tests off the live desktop.** `IAppBar` with `FakeAppBar`, `BarDeps::themeDetector` and `screenGeometry`, `EnvLookup` for config, `ProcessLauncher` for callbacks, the `CpuDataSource`/`MemoryDataSource`/`BatteryDataSource` interfaces, and `startPolling = false` on the widgets. `--register-app-bar` being opt-in is the right default.
- **Pure functions pulled out of Qt classes.** `computePopupPosition`, `computeTooltipPosition`, `computeAppBarRect`, `resolveBarScreens`, `parseHotkey`, `routeHotkeyScreen`, `quoteCmdLine`, `graphPoints`, `buildSmoothPath`, `pyFormat`. These are testable without a `QApplication` and are tested.
- **Hardening and warnings.** `/W4 /WX /permissive- /guard:cf /sdl /utf-8` and `/GUARD:CF /DYNAMICBASE /NXCOMPAT`, applied to our own targets only through the `yasb_options` interface library. vcpkg baseline pinned, Qt version pinned, schema compiled into the binary, `windeployqt` for tests so `ctest` works without `PATH` tricks.
- **Threading discipline.** Every cross-thread hand-off goes through `QMetaObject::invokeMethod(..., Qt::QueuedConnection)` or a queued signal (`cli_server.cpp`, `hotkeys.cpp`, `polling_worker.cpp`). The log observer is swapped under a mutex and invoked outside it (`log.cpp:20-34`). `PeekMessageW` before publishing the hotkey thread id (`hotkeys.cpp:134`) is the correct Win32 idiom.
- **Pimpl where the Win32 surface is large.** `TransitionEngine::Impl`, `PdhCpuDataSource::Impl`.
- **Honest comments.** Comments explain why, cite the Python source they port, and name the Win32 quirks they work around (`adaptive_frame.cpp:62-64`, `bar_window.cpp:298`, `bar_window.cpp:316`).

## 3. Architecture

### 3.1 Module map and dependency direction

Intended (from `docs/plans/...port-plan.md`):

```
app  →  widgets  →  core/{widgets,bar,css,config}  →  platform
```

Actual (from the CMake files):

```
yasb_core (one STATIC target) = app/* + core/* + platform/* + widgets/*
```

- `CMakeLists.txt:43-51` adds `app/*.cpp` to `yasb_core`.
- `core/CMakeLists.txt:8` adds `../platform` into the same target.
- `core/widgets/CMakeLists.txt:16-22` adds `../../widgets/stat_widgets.cpp`, `cpu_widget.cpp`, `memory_widget.cpp`, `battery_widget.cpp` into the same target. That is the core layer depending on concrete widgets.
- `app/hotkeys.cpp:16` includes `core/widgets/event_bus.h`; `app/app_services.cpp:13-16` includes `core/bar` and `core/config`. Those are allowed directions, but nothing checks them.

Consequence: an agent can include anything from anywhere and the build will not complain. Over 15 more widgets and the taskbar/systray work, this is how a port becomes a ball of mud.

Recommendation: split into targets with `PRIVATE`/`PUBLIC` link lines that encode the arrows, in this order of cost:

| Target | Sources | Links PUBLIC | Links PRIVATE |
|---|---|---|---|
| `yasb_platform` | `platform/` | `Qt6::Core` (for `QMetaType`) | `pdh setupapi psapi` |
| `yasb_config` | `core/config/` + generated schema | `nlohmann_json`, `Qt6::Core` | `yaml-cpp` |
| `yasb_css` | `core/css/` | `Qt6::Widgets` | |
| `yasb_ui` | `core/bar/`, `core/widgets/`, `core/util/` | `yasb_css`, `yasb_config`, `Qt6::Widgets` | |
| `yasb_widgets` | `widgets/` | `yasb_ui`, `yasb_platform` | |
| `yasb_app` | `app/` (minus `main.cpp`) | `yasb_widgets` | |
| `yasb-native` | `app/main.cpp` | | `yasb_app` |

Also move the `windowsapp runtimeobject` link off the executable and onto whichever library actually uses WinRT once one does. Today `winrt::init_apartment` in `main.cpp:158` is the only WinRT call, and `WIL::WIL` is linked but not included anywhere.

### 3.2 Global mutable state

Process-wide singletons and function statics the code relies on:

| What | Where | Risk |
|---|---|---|
| `EventBus::instance()` | `core/widgets/event_bus.h:14` | every `BaseWidget` connects in its constructor; tests share state across cases |
| `WidgetRegistry::instance()` | `core/widgets/registry.cpp:9` | `registerStatWidgets()` guards with a function-static bool (`widgets/stat_widgets.cpp:11`) |
| `GlobalState::instance()` | `core/bar/theme_class.cpp:10` | |
| `SchemaValidator::embedded()` | `core/config/schema_validator.cpp:461` | fine: immutable after construction |
| log file, observer, debug flag | `app/log.cpp:15-18` | process-global by nature; acceptable |
| `tooltipOptions()` static | `core/widgets/tooltip.cpp:17` | |
| `processLauncher()`, `systemFunctions()` | `core/widgets/callbacks.cpp:67-80` | mutable from anywhere |
| one `PollingWorker` per stat type | `widgets/cpu/cpu_widget.cpp:16-26` and the memory/battery twins | parented to `QCoreApplication`, so it survives widget rebuilds; the first widget's `update_interval` wins for all later ones silently |
| `static TransitionEngine engine` | `app/main.cpp:71` | see finding RS-1 |

None of these is wrong in isolation. Together they make the "construct an application" path implicit. Recommendation: introduce one composition root (an `Application` or `Runtime` struct built in `main`, owning the engine, registry, event bus and services) and pass references down. The seams already exist; this is mostly moving ownership, not rewriting logic. Do it before the widget count grows.

### 3.3 Two ways to read config

`core/config/options/*.h` are generated typed structs with `from_json`, exposed through `LoadResult::widgetOptions<T>()`. They are used by exactly one test (`tests/config/test_loader.cpp:195`). Every widget instead reads the raw `nlohmann::json` with `.value(key, default)` and restates the schema defaults in C++ (`cpu_widget.cpp:41,59,62`, `system_widget_helpers.h:42-59`, `bar_window.cpp:47-86`). Two sources of truth for defaults will drift, and the schema-driven validation already filled the defaults in, so the restated defaults are dead weight that can only be wrong.

Recommendation: pick the typed structs. Widgets take `const options::CpuConfig&` (or whatever their type is) from `WidgetContext`. Delete the restated defaults. Keep `generate_options.py` as the only way those headers change and verify that in CI (section 6).

### 3.4 Error-handling policy

There is none written down, and the code shows three styles:

- Return values and `std::optional` at API boundaries (config loader, hotkeys, battery). Good.
- Exceptions used internally and caught near the throw (`backdrop.cpp`, `label_format.cpp` `FormatError`, `callbacks.cpp:139-144`). Acceptable.
- Exceptions that can escape into Qt: `nlohmann::json::at()` and `get<T>()` in `app/main.cpp:85-87`, `std::stoi` on a format width in `label_format.cpp:66`, `nlohmann` `type_error` from `readField` in the generated headers. An exception propagating out of a Qt slot, event handler or `QApplication::exec()` is a `std::terminate` with no log line.

Recommendation: write the policy into `CLAUDE.md` and enforce it in review: exceptions never cross a Qt boundary (slot, event handler, `paintEvent`, callback invoked by Qt). Each entry point from Qt into our code either uses non-throwing APIs or wraps in a `try` that logs. Add a `qInstallMessageHandler`-adjacent top-level guard in `main` that logs and exits non-zero on an uncaught exception, so a crash during the user's session leaves evidence in `yasb-native.log`.

### 3.5 Style consistency

| Aspect | Variants seen | Where |
|---|---|---|
| Indent | 2 spaces (most files), 4 spaces | `core/config/*.cpp,h`, `core/config/options/*.h` (and `generate_options.py` emits 4) |
| Member naming | `name_` (app, bar, css, platform, widgets) vs `m_name` (config `SchemaValidator`, `core/widgets/base_widget.h`, `registry.h`, `polling_worker.h`, `callbacks.h`, `popup.h`) | 21 vs 6 headers |
| Access specifier indent | ` public:` (Google) vs `public:` (column 0) | `schema_validator.h:35` |
| Include form | `"core/widgets/base_widget.h"` vs `"base_widget.h"` | `core/widgets/*.cpp` use relative, everything else rooted |
| Qt string literals | `QStringLiteral(...)`, `u"..."_s`, `QLatin1String` | css uses `_s`, the rest `QStringLiteral` |
| Win32 header hygiene | `NOMINMAX` re-defined locally although `yasb_options` already defines it | `callbacks.cpp:8-10`, `backdrop.cpp:6-8` |

This is exactly what happens when several agents each write a unit without a formatter in the loop. Fix once with a single formatting commit, decide the member convention (the `_` suffix is the majority and the Google style the `.clang-format` already selects), and enforce with `clang-format` plus `clang-tidy`'s `readability-identifier-naming`.

## 4. Security

### 4.1 Threat model (not written down today)

The application runs as the interactive user, reads the user's own `config.yaml` and `styles.css`, and by design executes shell commands from that config (`exec` callback → `cmd.exe /c`, `callbacks.cpp:67-75`). The config is therefore code, and the CSS preprocessor following `@import` and `url()` paths anywhere on disk (`preprocessor.cpp:224-259`) is consistent with that. This matches the Python build and is acceptable, but it must be stated in a `SECURITY.md` so that agents do not "fix" it in either direction (neither sandboxing the config nor widening the IPC surface).

### 4.2 Findings

| ID | Severity | Finding | Location | Recommendation |
|---|---|---|---|---|
| SEC-1 | High | No `LICENSE`; MIT-derived code without notice. | repo root, `rules.h:50`, `transition_engine.cpp:1-3` | Add `LICENSE` and `THIRD_PARTY_NOTICES.md` with the qt-css-engine MIT text and commit `fadd1a992` reference. Blocker for a public repository, which the ARM64 hosted runners require. |
| SEC-2 | Medium | Named pipes are created with a `nullptr` security descriptor and without `PIPE_REJECT_REMOTE_CLIENTS`. Any local process that can open the pipe can issue `stop`/`reload`/`show-bar`/`hide-bar` and read the live log stream. | `app/cli_server.cpp:22-25` | Pass `SECURITY_ATTRIBUTES` with an SDDL restricting to the current user SID (`D:(A;;GA;;;<SID>)`), add `PIPE_REJECT_REMOTE_CLIENTS`, and cap `kBufferSize` reads with `ERROR_MORE_DATA` handling. Same for the log pipe. |
| SEC-3 | Medium | CI supply chain: actions referenced by floating tags (`actions/checkout@v4`, `actions/cache@v4`, `actions/setup-python@v5`, `actions/upload-artifact@v4`, third-party `ilammy/msvc-dev-cmd@v1`); `pip install aqtinstall` unpinned; no `permissions:` block so the token has default write scopes. | `.github/workflows/build.yml` | Pin every action to a commit SHA with a version comment, pin `aqtinstall==x.y.z` (optionally with `--require-hashes`), add `permissions: contents: read` at workflow level, add Dependabot for `github-actions` and `pip`. |
| SEC-4 | Low | Undocumented Windows APIs with hand-laid struct layouts: `SetWindowCompositionAttribute` (`backdrop.cpp:45-54`), `NtQuerySystemInformation` class 80 with a local `MemoryListInformation` (`memory.cpp:14-23,72`), `RtlGetVersion`, numeric `DWMWA_*` constants. All are guarded and fall back on failure. | as listed | Keep an allowlist in `docs/` naming each undocumented API, the Windows build it was verified on, and its fallback. Require that list to be updated when an agent adds one (the plan already lists several more coming in control_center, taskbar, wallpapers). |
| SEC-5 | Low | `keybd_event` is deprecated and injectable by any process; `SendInput` is the supported API. | `callbacks.cpp:83-88` | Replace with `SendInput`. |
| SEC-6 | Low | Log file appends forever, no rotation or size cap; the log pipe also broadcasts every line to any connected client. | `app/log.cpp:38-50` | Add a size cap with one rollover file. Never log config values that could carry secrets (the github token lives in `%LOCALAPPDATA%`, not in config, today). |
| SEC-7 | Info | Compile hardening is good. Missing optional flags: `/Qspectre`, `/CETCOMPAT` (x64 only), `/analyze` in CI, `/external:anglebrackets /external:W0` so third-party headers never block `/WX`. | `CMakeLists.txt:39-42` | Add `/Qspectre` and `/external:*` now; `/analyze` in a CI job (section 6). |

## 5. Runtime safety and correctness

Findings that are cheap to fix now. None is a parity issue; all are the kind of thing that produces a crash report six months in.

| ID | Severity | Finding | Location | Recommendation |
|---|---|---|---|---|
| RS-1 | High | `static yasb::css::TransitionEngine engine;` is a function-local static `QObject` with an event filter installed on `qApp` and `QVariantAnimation` children. It is destroyed during static destruction, after `QApplication` is gone. Qt does not support destroying `QObject`s after the application object; this will surface as warnings or a crash on exit, typically only on the user's machine. | `app/main.cpp:71` | Make the engine a local in `main` (or a member of the composition root from 3.2) so it is destroyed before `app`. |
| RS-2 | High | `nlohmann::json::operator[]` on a `const` object with an absent key is undefined behaviour (an assertion in debug, a null deref in release). The loader does `bar.value()["widgets"]` and `placement[column]` on a `const OJson&`. It works only because root validation filled the defaults first. | `core/config/loader.cpp:183-185`; also `app/main.cpp:106,120` on `result.config["bars"][pick.barName]` | Use `find()`/`at()` or `value()`. Add a `clang-tidy` or grep check that flags `operator[]` on const json. |
| RS-3 | Medium | Null-pointer paths on headless or odd multi-monitor setups: `primary->name()` when `primaryScreen()` returns null; `screen->name()` when `screenByName` returns null (it can, since it returns `nullptr` on no match). | `app/main.cpp:92-101`, `core/bar/bar_window.cpp:93-97` | Check and log; exit with a distinct code. |
| RS-4 | Medium | `std::regex` on MSVC is recursive and slow; long inputs can overflow the stack. It is used on every YAML scalar at load and on every format spec at every label update. | `core/config/loader.cpp:69-80`, `core/widgets/label_format.cpp:58` | Replace with hand-written scanners (both grammars are tiny) or `QRegularExpression`, which the CSS code already uses. Bonus: one regex engine in the binary. |
| RS-5 | Medium | The app-wide event filter does work on every event for every `QObject`. `matchSegment` constructs a `QRegularExpression(u"\\s+")` per call; `cascade` is O(rules) per widget per event and re-sorts each time; `evaluate` runs the cascade twice (active and base). Fine for three widgets; the taskbar will have dozens. | `core/css/transition_engine.cpp:257,288-307,477-485` | Hoist the regex to a static, cache the compiled cascade per widget (invalidate on `DynamicPropertyChange`/`ParentChange`/stylesheet reload), and add a micro-benchmark to the tests so regressions are visible. |
| RS-6 | Low | `HotkeyService::start` spin-waits with `Sleep(1)`. | `app/hotkeys.cpp:122` | Use a `std::binary_semaphore` or `std::condition_variable`. |
| RS-7 | Low | `commandLoop` does one `ReadFile` on a message-mode pipe; a message over 64 KiB returns `ERROR_MORE_DATA` which is treated as failure, and the partial buffer is discarded. | `app/cli_server.cpp:120-129` | Loop on `ERROR_MORE_DATA` or reject oversize messages explicitly. |
| RS-8 | Low | PDH swap query lives in file-scope globals that are never closed; the CPU source uses a proper `Impl` with destructor. Two patterns for the same thing. | `platform/memory.cpp:27-30` | Move the swap counter into `WindowsMemoryDataSource` like `PdhCpuDataSource::Impl`. |
| RS-9 | Low | First widget's `update_interval` silently wins for the shared `PollingWorker`; a second CPU widget with a different interval gets the first one's cadence. | `widgets/cpu/cpu_widget.cpp:16-26` | Either key the worker by interval or log when the requested interval is ignored. |
| RS-10 | Low | `registerStatWidgets` guard is a plain `static bool`, not thread-safe (fine today, called from `main`). | `widgets/stat_widgets.cpp:11` | Use `static std::once_flag` or a function-local static initialiser. |
| RS-11 | Low | `AppServices::requestRestart` spawns the replacement and then quits; if the replacement fails to acquire the mutex within 10 s it exits with an error and the user has no bar. There is no back-off against a restart storm from repeated `screenAdded`/`screenRemoved` events (docking a laptop fires several). | `app/app_services.cpp:103-118` | Debounce screen events (250 ms is enough) and log the replacement's failure reason to the shared log. |
| RS-12 | Info | Public headers include `<windows.h>`: `app/cli_server.h`, `app/hotkeys.h`, `app/single_instance.h`. `WIN32_LEAN_AND_MEAN` and `NOMINMAX` are set globally so the damage is limited, but every consumer still pays the compile cost and gets the macro namespace. | as listed | Hide `HANDLE` behind `void*` or `wil::unique_handle` in the header, or pimpl. |

## 6. Build, CI and test infrastructure

### 6.1 What the workflow does today

`build.yml`: three matrix entries (x64 release, ARM64 release, ARM64 debug), vcpkg from the pinned baseline with a file cache, Qt via `aqtinstall`, configure/build/test/deploy with presets, artifact upload. Triggers on every `push` and every `pull_request`, so a PR from a branch in the same repo runs twice.

### 6.2 Gaps

| ID | Gap | Why it matters for autonomous agents |
|---|---|---|
| CI-1 | No format check | Agents produce the style drift in 3.5 and nothing stops the merge. |
| CI-2 | No static analysis (`cl /analyze`, `clang-tidy`, CodeQL) | RS-2 style bugs and the naming rule can be machine-checked. `/analyze` costs nothing on MSVC. |
| CI-3 | No sanitizer run | `/fsanitize=address` on the x64 debug build catches the class of bugs in section 5. Verify ARM64 ASan support for the installed MSVC toolset before adding an ARM64 variant. |
| CI-4 | No generated-file drift check | `core/config/options/*.h` can diverge from `schema/schema.json` silently. Add a job that runs `generate_options.py` and fails on `git diff --exit-code`. |
| CI-5 | No `compile_commands.json` | `CMAKE_EXPORT_COMPILE_COMMANDS=ON` in the presets is required by `clang-tidy` and helps every IDE. |
| CI-6 | Test invocation hides failures | `ctest --preset` without `--output-on-failure` and without per-test `--timeout`; no upload of `Testing/Temporary/LastTest.log` on failure. |
| CI-7 | No `timeout-minutes`, no `concurrency` cancel | Hung Qt installs or tests hold runners. |
| CI-8 | Qt re-downloaded every run | Cache `C:/Qt/<version>/<arch>` keyed on version and arch. |
| CI-9 | Python and PowerShell tooling untested in CI | `tools/parity/tests/test_export.py` needs the YASB checkout (cannot run hosted), but `tools/ab/tests/Test-CompareScreenshots.ps1` and `generate_options.py` can. Add `ruff` and `PSScriptAnalyzer`. |
| CI-10 | No conventional-commit enforcement | Commit subjects are mixed: `feat(config): ...` and `build: ...` next to `Implement U7 app services` and `Add native CPU memory and battery widgets`. Enforce on PR title (squash merge) or on each commit. |
| CI-11 | No branch protection evidence | Nothing in the repo shows that `main` requires the build check. Set required checks, forbid direct push, prefer squash merge so one PR equals one conventional commit. |
| CI-12 | Deploy runs on every PR | `--target deploy` and the artifact upload only need to run on `main` and tags. |

### 6.3 Test suite assessment

Strengths: parity vectors; `QT_QPA_PLATFORM=offscreen`; real named-pipe round-trip tests for the CLI (`test_cli_protocol.cpp`); a real mutex test for single instance; every widget constructible with `startPolling = false`.

Gaps:

| ID | Gap | Recommendation |
|---|---|---|
| T-1 | `AppServices`, `TrayIcon`, `log.cpp`, `WidgetBuilder` have no tests. | `AppServices` is testable if `SingleInstance`, `CliServer` and `TrayIcon` are injected like `BarDeps`. |
| T-2 | Sleep-based timing in `test_single_instance.cpp:23-25` and polling loops in `test_cli_protocol.cpp`. | Replace with event-driven waits (`WaitForSingleObject` on the mutex, overlapped pipe waits) or generous deadlines with a clear failure message. Flaky tests are the first thing autonomous agents learn to retry instead of fix. |
| T-3 | Parsers have no fuzz targets: YAML scalar resolver, CSS tokenizer (`rules.cpp`), `pyFormat`, `parseHotkey`, `quoteCmdLine`, `parseCallback`. | Add libFuzzer targets (MSVC x64 supports `/fsanitize=fuzzer`) behind a `YASB_FUZZ` option; run on a schedule, not on every PR. |
| T-4 | Three different ways to get a `QApplication` in tests (`test_startup.cpp` constructs one inline, `test_system_widgets.cpp` has a `g_app` global, others assume one exists). | One `gtest` `Environment` in a shared `test_main.cpp`. |
| T-5 | No coverage number. | OpenCppCoverage on the x64 job, upload as artifact; set a floor once measured. Do not chase a percentage; use it to find the untested files in T-1. |
| T-6 | `tests/CMakeLists.txt` has a duplicated section comment (`--- U6 ---` twice) and grows by append. | Minor; use `file(GLOB_RECURSE)` with `CONFIGURE_DEPENDS` or one `add_subdirectory` per area. |

### 6.4 CMake details

- `CMakeLists.txt` assumes MSVC flags unconditionally. State it: `if(NOT MSVC) message(FATAL_ERROR "yasb-native builds with MSVC only")`.
- Add `CMAKE_EXPORT_COMPILE_COMMANDS ON` to the base preset.
- Consider `CMAKE_COMPILE_WARNING_AS_ERROR` instead of hand-rolled `/WX` so CI can flip it off for exploratory builds.
- `WIL::WIL` is linked and unused; either use it (the handle wrappers would simplify `battery.cpp:79-82`, `single_instance.cpp`, `cli_server.cpp`) or drop the dependency.

## 7. What to add so agents stay inside the rails

This is the enforcement layer the verdict asked for, in the order it pays off.

### 7.1 Repository files

| File | Content |
|---|---|
| `LICENSE`, `THIRD_PARTY_NOTICES.md` | See SEC-1. |
| `CLAUDE.md` (also readable as `AGENTS.md`) | Build/test commands verbatim from `docs/building.md`; the layering table from 3.1; the style decisions from 3.5; the error-handling policy from 3.4; "the parity vectors are the oracle, regenerate with `tools/parity`, never hand-edit"; "`core/config/options/*.h` are generated, edit `generate_options.py` and rerun"; "never run the binary without `--config-home` in tests or tooling, never pass `--register-app-bar` outside the A/B harness"; the undocumented-API allowlist rule from SEC-4; conventional commits with the allowed scopes (`config`, `css`, `bar`, `widgets`, `app`, `platform`, `parity`, `ci`, `build`, `docs`); definition of done (format clean, generated files clean, build and `ctest` green on the debug preset). |
| `SECURITY.md` | The threat model from 4.1 in five sentences. |
| `.editorconfig` | `indent_style = space`, `indent_size = 2`, `end_of_line = lf`, `insert_final_newline = true`, `charset = utf-8`; `[*.ps1]` 4 spaces; `[*.py]` 4 spaces. |
| `.clang-tidy` | `bugprone-*`, `cert-*`, `concurrency-*`, `misc-*`, `modernize-*`, `performance-*`, `readability-identifier-naming` (members `lower_case` with `_` suffix, or whatever 3.5 decides), with the Qt-incompatible checks disabled (`modernize-use-trailing-return-type`, `cppcoreguidelines-owning-memory`, `cppcoreguidelines-pro-type-reinterpret-cast` are the usual ones). |
| `.pre-commit-config.yaml` | `clang-format`, `trailing-whitespace`, `end-of-file-fixer`, `check-yaml`, `check-json`, `gitleaks`, `conventional-pre-commit`. Keep it under two seconds; builds do not belong here. |
| `.github/CODEOWNERS`, `.github/PULL_REQUEST_TEMPLATE.md`, `.github/dependabot.yml` | Owner review on `core/config/schema_validator.cpp`, `tools/parity/**`, `tests/vectors/**`, `.github/**`. PR template with the definition-of-done checklist. Dependabot for `github-actions` and `pip`. |
| `tools/check.ps1` | One script that runs: `clang-format --dry-run -Werror`, `generate_options.py` + `git diff --exit-code`, `cmake --build --preset arm64-debug`, `ctest --preset arm64-debug --output-on-failure`. This is what every hook and every agent calls; CI calls the same script so local and remote agree. |

### 7.2 CI jobs

Add to `build.yml` or split into separate workflows:

| Job | Runner | Steps | Blocking? |
|---|---|---|---|
| `lint` | `ubuntu-latest` | `clang-format --dry-run -Werror` over `app core platform widgets tests tools/yasbc-native`; `ruff check tools core/config/options/generate_options.py`; `PSScriptAnalyzer` on `tools/ab`; commitlint on PR title | yes |
| `generated` | `ubuntu-latest` | `python -I core/config/options/generate_options.py` then `git diff --exit-code core/config/options` | yes |
| `analyze` | `windows-latest` | configure x64 with `/analyze /analyze:external-`; `clang-tidy` via `compile_commands.json` with `clang-cl`; CodeQL C++ if the repo is public | yes once the baseline is clean |
| `asan` | `windows-latest` | x64 Debug preset with `/fsanitize=address`, `ctest --output-on-failure` | yes |
| `build` (existing) | matrix | add `permissions: contents: read`, `concurrency`, `timeout-minutes: 60`, Qt cache, `ctest --output-on-failure --timeout 120`, upload `LastTest.log` on failure, deploy only on `main`/tags, pin action SHAs | yes |
| `fuzz` | `windows-latest`, `schedule: weekly` | libFuzzer targets from T-3, 10 minutes each, upload crashes | no |

Trigger only `pull_request` plus `push` to `main` and tags, to stop the double run.

### 7.3 Claude Code hooks (`.claude/settings.json`)

These run on the agent's machine, before anything reaches CI. Each one is a shell command the harness executes; exit code 2 blocks the tool call.

| Event | Matcher | Command | Purpose |
|---|---|---|---|
| `PostToolUse` | `Edit\|Write` | `clang-format -i` on the edited path when it ends in `.cpp`/`.h` and is under `app core platform widgets tests tools/yasbc-native` | formatting never drifts |
| `PreToolUse` | `Edit\|Write` | exit 2 when the path is under `tests/vectors/` or `core/config/options/*.h` (except via the generator) or is `schema/schema.json` | oracle and generated files stay generated |
| `PreToolUse` | `Bash` matching `git commit` | run `tools/check.ps1 -Fast` (format and generated-file checks only) and a conventional-commit regex on the message | nothing unformatted is committed; subjects stay conventional |
| `PreToolUse` | `Bash` matching `yasb-native.exe` without `--config-home` or with `--register-app-bar` | exit 2 unless `tools/ab` is the caller | the live desktop is never touched by accident |
| `Stop` | | `tools/check.ps1` full; exit 2 with the failing step's output if red | an agent cannot declare done on a red build |

Keep the hook scripts in `tools/hooks/` under version control so every agent session on every machine gets the same rails; the `.claude/settings.json` in the repo only references them.

### 7.4 Review rules to write down

- Every new Win32 call to an undocumented API is added to the allowlist with the build number it was verified on.
- Every new widget ships: the typed options struct wired from `WidgetContext`, a label-vector exporter in `tools/parity/vec_labels.py`, its vectors, a C++ parity test, and a row update in `docs/parity-checklist.md`.
- Every new thread or `QThread` documents which thread owns what and how it is stopped, next to the class.
- No new singleton without a line in the 3.2 table and a reason.

## 8. Documentation

- `docs/building.md` says `registerStatWidgets()` is intentionally not called from `app/main.cpp`; the working-tree `main.cpp:105` calls it. Update when that change lands.
- `README.md` points at the plan but not at the build doc, the parity README, or the checklist. Add a short map.
- The plan document is the architectural record and is good. Add `docs/architecture.md` with the layering table and the ownership/threading summary from sections 3.1 and 3.2 so agents do not have to reconstruct it from CMake files.
- The undocumented-API allowlist (SEC-4) and the error-handling policy (3.4) have no home yet; `docs/architecture.md` is the place.

## 9. Suggested order of work

Each item is one PR. None changes behaviour the parity vectors observe, so all can land green.

1. `LICENSE`, `THIRD_PARTY_NOTICES.md`, `SECURITY.md`, `.editorconfig`, `CLAUDE.md`, `CODEOWNERS`, PR template, Dependabot. (docs/chore)
2. Harden `build.yml`: permissions, pins, concurrency, timeouts, Qt cache, `--output-on-failure`, triggers, deploy on main only. Add `lint` and `generated` jobs; `lint` initially non-blocking. (ci)
3. One formatting commit: `clang-format` over the tree, fix `generate_options.py` to emit two-space indent, rename `m_` members to the `_` suffix, root all includes. Then flip `lint` to blocking and add the hooks. (style)
4. `tools/check.ps1` and `.claude/settings.json` hooks. (chore)
5. RS-1, RS-2, RS-3: engine lifetime, const `operator[]`, null screens, plus the top-level exception guard. (fix)
6. SEC-2: pipe security descriptor and remote-client rejection. (fix)
7. Split `yasb_core` into the layered targets. (build)
8. Typed options in the three existing widgets; delete restated defaults; `WidgetContext` carries the struct. (refactor)
9. `.clang-tidy`, `analyze` job, `asan` job; fix what they find. (ci)
10. RS-4 and RS-5: replace `std::regex`, cache the cascade, add the micro-benchmark. (perf)
11. T-1 to T-4: inject `AppServices` collaborators, shared test `Environment`, event-driven waits, fuzz targets. (test)

After step 4 the rails exist and autonomous widget work (U8 onward in the plan) can proceed in parallel with steps 5 to 11.
