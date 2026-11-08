# Agent guide (waku)

Read this before changing anything. It applies to every agent session and to humans. It is the
single source of truth for how to work in this repository. Background: `docs/architecture.md`,
`docs/building.md`, `SECURITY.md`, `src/app/README.md`.

waku is a C++20/Qt 6.10 Qt Quick desktop shell runtime for Windows, configured in QML, with
Quickshell-compatible modules. Python YASB is a reference for how Windows behaves (undocumented
APIs, edge cases), not a behavioural spec: nothing in this repository has to match it.

## Build and test

Use `pwsh`, never `powershell` or `powershell.exe`, for script execution, and use `pwsh` for
everything that is shell specific (paths, environment variables, processes, files, build and test
commands). Do not mix Bash and PowerShell for the same task.

### Shell hygiene (no escaping churn)

- Write Windows paths in single-quoted PowerShell literals, build them with `Join-Path`, and read
  well-known folders from `$env:USERPROFILE` and `$env:LOCALAPPDATA` instead of typing them. Use
  `-LiteralPath` for `Remove-Item` and `Copy-Item`.
- More than a few lines, or anything that needs quoting, goes into a `.ps1` or `.py` file that is
  then run. Do not paste long one-liners.
- Python from a shell uses raw strings (`r'...'`) or reads its arguments from a file; a backslash
  path in a normal string is a bug (`\w`, `\.`).
- Bash, where it exists, only gets forward-slash paths (`/<drive>/<dir>`) and simple git or file
  commands.
- Do not chain a delete or overwrite with a `Start-Process` of a path with spaces in one command;
  run them as separate calls.
- One shell per task: PowerShell. The Bash tool is only for `git`, `gh` and one-line file
  commands. Never write PowerShell from Bash, and never put `$var` in a Bash heredoc that holds a
  `.ps1`.
- Multi-line code goes through the Write tool into a file (the scratchpad), then runs by path
  (`pwsh -NoProfile -File <path>`). No `@'...'@` inside `@'...'@`. No Python inside a Bash heredoc.
- Do not name a PowerShell variable `$home`, `$args`, `$input`, `$error`, `$pid`, `$host` or
  `$matches`; they are read-only or automatic.
- Python: `python -I file.py`. Use `os.devnull`, never `/dev/null`.
- Edit files with the Edit tool; it keeps CRLF. Do not write replace helper scripts.
- Do not pipe a build, check or test run into `head`, `tail` or `Select-String`. Redirect it
  (`*> $env:TEMP\x.log`), read `$LASTEXITCODE`, then read the tail of the log.
- Wait for work with `run_in_background` (you get a notification) or `Monitor`. Do not poll with
  `sleep`.
- `gh ... --jq` with braces or pipes: do not nest it in `until` or `$(...)`. Call `gh` plainly and
  filter in PowerShell.
- Check that a path exists (Glob) before you Grep or Read it. Do not guess file names.

Run every step in one `cmd.exe` process after the MSVC environment script; environment changes do
not survive between separate `cmd /c` calls. `vcvars*.bat` live in
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build`. They point
`VCPKG_ROOT` at the bundled vcpkg, so set it to `C:\vcpkg` afterwards.

```powershell
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build'

# ARM64 debug: configure, build, test (the default preset)
cmd /c "call `"$vc\vcvarsarm64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-debug && cmake --build --preset arm64-debug && ctest --test-dir build\arm64-debug --output-on-failure --timeout 120 -j"

# ARM64 release
cmd /c "call `"$vc\vcvarsarm64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-release && cmake --build --preset arm64-release && ctest --test-dir build\arm64-release --output-on-failure --timeout 120 -j"

# x64 release, cross-built from the ARM64 host (tests run under emulation)
cmd /c "call `"$vc\vcvarsarm64_amd64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset x64-release && cmake --build --preset x64-release && ctest --test-dir build\x64-release --output-on-failure --timeout 120 -j"
```

Targets: `waku` (the bar), `wakuc` (its console client), `wakukeys` (the hotkey daemon, a GUI
program), `WakuTrayHook[_arm64]` (the DLL injected
into Explorer), `waku_tests` (all tests, defined in `tests/`) and `deploy` (assembles `dist/waku`;
see `docs/building.md`). `ctest` needs `QT_QPA_PLATFORM=offscreen`; the CMake test properties set
it, so `ctest --test-dir` works without the test preset. A ctest test is one gtest suite (one
process); a single case runs with `waku_tests --gtest_filter=Suite.Case`. Presets live in `CMakePresets.json`;
`compile_commands.json` is written to `build\<preset>`.

Run `clang-format -i` on touched files before `tools\check.ps1`; the `format` step fails
otherwise. Iterate with `tools\check.ps1 -Fast`; run the full check once at the end. Never call
`cl` or `cmake` outside the `cmd /c` + `vcvars` call above. On a Linux runner or in a CI worktree
the build cannot run: use `-Fast` only.

## Definition of done

`tools\check.ps1` exits 0. Its steps, in order:

1. `format`: clang-format dry run over `src/` and `tests/`.
2. `structure`: rules S1-S8 (below).
3. `json-subscript`: no `const nlohmann::json::operator[]` in product code.
4. `build`: configure and build the `arm64-debug` preset (own targets use `/W4 /WX`).
5. `qmllint`: the `all_qmllint` target over every QML module and the sample shells in `examples/`;
   fails when the warning count rises above `tools/qmllint-baseline.txt` (a ratchet: lower the
   number when it falls, never raise it).
6. `test`: `ctest` over `waku_tests`.

Options: `-Fast` runs steps 1-3 only; `-Staged` checks only the staged files (the pre-commit hook
uses it); `-Analyze` runs steps 1-3 plus MSVC `/analyze` and clang-tidy in an isolated build
directory instead of steps 4-6 (see `docs/building.md`). `-SkipFormat` and `-Preset <name>` also
exist.

Git hooks in `tools/hooks` run the fast checks on staged files and validate the commit message.
Enable them once per clone:

```powershell
git config core.hooksPath tools/hooks
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

`.git-blame-ignore-revs` lists mechanical formatting commits so `git blame` skips them.

## Structure

Layout: `src/{kernel,control,host,ui,features/<f>,quickshell,app/<exe>}`, `tests/` (the test main
and the cross-feature tests), `examples/config` (the sample config home), `cmake/`, `tools/`,
`docs/`. Dependencies point one way: `app` -> `quickshell` -> `features` -> (`host`, `control`,
`ui`) -> `kernel`. The include root is `src/`. Every unit is a static library or QML module; a
feature is `waku_<f>`. `src/quickshell/` holds the Quickshell compatibility modules
(`Quickshell`, `Quickshell.Io`, ...): registrations and QML over the engine and the features, no
engine logic; its `README.md` says where each Quickshell type is implemented.

The `structure` step of `tools\check.ps1` enforces rules S1-S8. The rule text is the table at the top
of that step in the script, which is exact; do not restate it here. In short:

- S1: every file under `src/` belongs to exactly one unit and is listed in its `CMakeLists.txt`.
- S2: includes are rooted at `src/` and stay inside the unit, `kernel/`, `control/` and the root
  headers of `DEPENDS` features. `quickshell` may also include `host/qml/` and the root and `qml/`
  headers of any feature; only `app/` and tests include `quickshell/`.
- S3, S4: Windows SDK and WinRT headers only in `win32/` folders (and the tray hook, `app/`,
  tests); `QtQml`/`QtQuick` and `QML_*` macros only in `qml/` folders (and `testing/`, `app/`,
  tests); `QtWidgets` nowhere.
- S5: only tests include `testing/` (fakes).
- S6, S7: a feature's QML uses services and widgets of its own feature or its `DEPENDS` only;
  QML-facing header names are unique.
- S8: `DEPENDS` names existing features and has no cycle.

Design and the dependency table: `docs/architecture.md`.

Shells are configuration, not engine. A ported or user-written shell (celeste, a launcher, a dock)
lives in a config home outside this repository and carries its own tokens, palette, layout and styling.
The engine holds only what QML cannot do and generic Quickshell compatibility. Do not add a type,
property, name or default that exists only for one shell; the shell builds it in its own QML. A shell
under a licence that is not MIT stays out of this repository entirely.

## Adding a feature

1. Create `src/features/<f>/` with only the folders it needs: the root (domain: interfaces, value
   types, UI-free logic; Qt Core/Gui only), `win32/` (Windows implementation, `_win` suffix where a
   root header has the same name), `qml/` (QML-facing C++ and the widgets' `.qml`), `testing/`
   (header-only fakes), `tests/` (gtest) and `hook/` only for a separate DLL.
2. Write `src/features/<f>/CMakeLists.txt` with one `waku_feature(NAME <f> ...)` call (arguments are
   in the comment at the top of `cmake/waku_feature.cmake`). Name every file; the folder name
   equals `NAME`.
3. Add `add_subdirectory(<f>)` to `src/features/CMakeLists.txt`, below every feature in its
   `DEPENDS`.
4. Declare `DEPENDS` for each feature whose root headers or QML types you use. Add only edges that
   exist; the table in `docs/architecture.md` lists them.
5. QML contributions go in `QML_SOURCES` (services, compiled into `waku.services`) and `QML_FILES`
   (widgets, compiled into `waku.widgets`). Give each new `.qml` and header a basename that is
   unique in its module. Do not edit `src/modules/`.
6. Put the interface behind a root header and the real thing in `win32/`, so tests use a fake from
   `testing/`. Tests go in `tests/` (`TESTS`), named after the feature (`TrayModel...`) so
   `ctest -R Tray` selects them.
7. Write `design.md` in the feature folder: what it is for, the decisions, the open points.
8. Update `docs/architecture.md` in the same commit: a row in the dependency table, one per new
   thread, one per new singleton, one per undocumented Windows API. Add the feature name to the
   commit scopes in `tools/hooks/commit-msg` and to this file.
9. Run `tools\check.ps1`.

## The kernel

`src/kernel/` is for helpers that meet all three tests: two or more units use it today (not
"might"), it has no domain knowledge (no "tray", "komorebi", "wallpaper"), and it changes for reasons
outside any one feature. A helper with one user lives in that user. A pull request that adds a kernel
file names its two users in the description. The kernel links Qt Core and Gui only; the Windows SDK
only under `kernel/win32/`.

## Style

- `.clang-format` (Google based, 2-space indent, 100 columns) is the formatter; run
  `clang-format -i` on every `.cpp`/`.h` you touch. clang-format lives on `PATH` or in
  `C:\Program Files\LLVM\bin`.
- Members use a trailing underscore: `name_`. Do not add new `m_name` members.
- Includes are rooted at `src/`: `#include "kernel/qt/app_data.h"`, never `"app_data.h"`.
- C++ namespaces are `waku::<unit>`: `kernel`, `control`, `host` or the feature name.
- Qt string literals: `QStringLiteral("...")`. Do not introduce `u"..."_s` or `QLatin1String` in new
  code except where an API needs a Latin-1 view.
- `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `UNICODE` come from the `waku_options` target; do not redefine
  them locally.
- `.editorconfig` defines whitespace and line endings: CRLF for C++ sources and docs, LF for QML and
  the git hooks.
- Prose (docs, comments, commit messages): short sentences, active voice, no filler, no emoji.

## Error handling

Exceptions never cross a Qt boundary: slots, event handlers, timers and any callback Qt invokes.
Every entry point from Qt into our code either uses non-throwing APIs (`json::value`, `find`,
`std::from_chars`, `std::optional` returns) or wraps its body in `try`/`catch` and logs. At API
boundaries return values or `std::optional`; exceptions thrown and caught inside one module are
fine. Never use the `const` `nlohmann::json::operator[]` on a key that may be missing. See
`docs/architecture.md`.

## Live desktop rules

- Tests and tooling always pass `--config-home <dir>` to `waku.exe`. Never run it against the
  user's real config (`%USERPROFILE%\.config\waku`) and never write to that folder.
- Tests never touch the user's real bar, wallpaper, tray or Explorer. Use `--no-tray` so a test run
  never hooks Explorer, and never change the wallpaper outside a `--dry-run`.
- To diagnose a live run, start waku with `--log-file <path>` (off by default; format and how to
  read it in `docs/logging.md`).
- Services ask `kernel::liveDesktop()` before touching the machine; it is false on the offscreen
  platform. No service tests `platformName()` itself.
- A live run reserves screen work area, hides the Windows taskbar, tiles windows and starts the key
  daemon. Tests and tools pass `--no-app-bar --keep-taskbar --no-keys --no-tiling` so a run leaves the
  desktop alone (the offscreen platform never reserves). `--safe` starts the built-in bar and ignores the config.

- Every `wakuc` call passes `--config-home <dir>`, the same one as the `waku.exe` it controls.
  Without it `wakuc` targets the user's real config.
- Start a test instance with `Start-Process -PassThru`, keep the PID, and stop that PID in a
  `finally`. Before you finish, list `waku*` processes and stop leftovers, including ones from
  deleted worktrees.
- A test that takes over keys, the taskbar or the tiler says so first and restores the user's
  setup before it ends.
- `Remove-Item` on `D:\waku-ports` itself is blocked. Delete files inside it.

## Housekeeping

When a feature or experiment ends, delete in the same change: probes, scratch builds, plans,
designs, handover docs, `dist-*` folders and stale scripts. Do not wait to be asked. Validate
against the running program or the source, not memory.

## Undocumented Windows APIs

Every call to an undocumented API (`GetProcAddress` on an unexported/undocumented symbol, a
hand-laid struct, a numeric constant missing from the SDK) must be added to the allowlist in
`docs/architecture.md` with the Windows build it was verified on and its fallback, in the same
commit. It must fail soft (fall back or skip) when the symbol is missing or the call fails.

## Threads and singletons

For every new thread or `QThread`: a comment next to the class saying which thread owns what and how
it is stopped, and a row in the threading table of `docs/architecture.md`. No new singleton without
a row (and a reason) in the singleton table there.

## Commits

Conventional commits: `type(scope): subject`, imperative, lower case, no trailing period.

- Types: `feat`, `fix`, `refactor`, `perf`, `test`, `docs`, `build`, `ci`, `chore`, `style`, `revert`.
- Scopes: the unit names `kernel`, `control`, `host`, `ui`, `quickshell`, `app`; the feature names `apps`, `audio`,
  `bluetooth`, `brightness`, `clock`, `color_scheme`, `control_center`, `desktop_state`, `github`, `keymap`, `media`, `network`,
  `notifications`, `quickshell_io`, `startup`, `system_stats`, `taskbar`, `tiling`, `tray`, `wallpapers`, `workspaces`; and
  `build`, `docs`, `test`, `style`, `chore`. The scope is optional. `tools/hooks/commit-msg` checks it.
- One logical change per commit; `tools\check.ps1` green before committing.
- Agent-authored commits end with the attribution trailer the session instructions give.
