# Agent guide (wabar)

Read this before changing anything. It applies to every agent session and to humans. Background:
`docs/architecture.md`, `docs/building.md`, `SECURITY.md`, `src/app/README.md`.

wabar is a C++20/Qt 6.10 Qt Quick bar for Windows, configured in QML. The retired Qt Widgets build
(yasb-native) is kept at the git tag `yasb-native-final` and in `docs/archive/`. Python YASB is a
reference for how Windows behaves (undocumented APIs, edge cases), not a behavioural spec: nothing in
this repository has to match it.

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
- Bash, where it exists, only gets forward-slash paths (`/d/yasb-native`) and simple git or file
  commands.
- Do not chain a delete or overwrite with a `Start-Process` of a path with spaces in one command;
  run them as separate calls.

Run every step in one `cmd.exe` process after the MSVC environment script; environment changes do
not survive between separate `cmd /c` calls. `vcvars*.bat` live in
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build`. They point
`VCPKG_ROOT` at the bundled vcpkg, so set it to `C:\vcpkg` afterwards.

```powershell
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build'

# ARM64 debug: configure, build, test (the default preset)
cmd /c "call `"$vc\vcvarsarm64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-debug && cmake --build --preset arm64-debug && ctest --test-dir build\arm64-debug --output-on-failure --timeout 120"

# ARM64 release
cmd /c "call `"$vc\vcvarsarm64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-release && cmake --build --preset arm64-release && ctest --test-dir build\arm64-release --output-on-failure --timeout 120"

# x64 release, cross-built from the ARM64 host (tests run under emulation)
cmd /c "call `"$vc\vcvarsarm64_amd64.bat`" && set VCPKG_ROOT=C:\vcpkg&& cmake --preset x64-release && cmake --build --preset x64-release && ctest --test-dir build\x64-release --output-on-failure --timeout 120"
```

The targets are `wabar` (the bar), `wabarc` (its console client), `WabarTrayHook[_arm64]` (the
DLL injected into Explorer) and `wabar_tests` (all tests, defined in `tests/`). The `deploy` target assembles `dist/wabar`: the
executables, the hook DLL, the Qt runtime (`windeployqt`), the built-in widget sources with `qmldir`
and `.qmltypes` under `modules/` (read-only reference for `qmllint -I <dist>/wabar/modules`) and
`examples/config`. `ctest` needs
`QT_QPA_PLATFORM=offscreen`; the CMake test properties set it, so `ctest --test-dir` works without
the test preset. Presets live in `CMakePresets.json`; `compile_commands.json` is written to
`build\<preset>`.

## Definition of done

`tools\check.ps1` exits 0. It runs the clang-format check, the `structure` check (rules S1-S8 of the
architecture proposal), the json-subscript check, the `arm64-debug` build and `ctest`.
`tools\check.ps1 -Fast` runs only the first three. Every own target builds with `/W4 /WX`.

`pwsh -NoProfile -File tools\check.ps1 -Analyze` runs the fast checks and opt-in local static
analysis instead of tests, in an isolated build directory. It requires clang-tidy; see
`docs/building.md`. Ordinary checks reset `WABAR_ANALYZE=OFF`; hooks remain fast-only.

Git hooks in `tools/hooks` run the fast checks on staged files and validate the commit message.
Enable them once per clone:

```powershell
git config core.hooksPath tools/hooks
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

`.git-blame-ignore-revs` lists mechanical formatting commits so `git blame` skips them.

## Layering

Dependencies point one way: `app` -> `features` -> (`host`, `control`, `ui`) -> `kernel`. The
`structure` step of `tools\check.ps1` enforces rules S1-S8 (includes, Windows and QML headers, QML use
across features, `DEPENDS`) on `src/` and `tests/`. `cmake/wabar_feature.cmake` declares a feature
(`wabar_feature(NAME <f> DEPENDS ... SOURCES ... WIN32_SOURCES ... QML_SOURCES ... QML_FILES ...
TESTS ... SYSTEM_LIBS ...)`, one line in `src/features/CMakeLists.txt`), and
`src/modules/qml_modules.cmake` builds the QML modules `wabar.services` and `wabar.widgets` from the
contributions of `src/`. The executables are in `src/app/`; the sample config home is
`examples/config`.

Details: `docs/architecture.md`.

## Style

- `.clang-format` (Google based, 2-space indent, 100 columns) is the formatter; run
  `clang-format -i` on every `.cpp`/`.h` you touch. clang-format lives on `PATH` or in
  `C:\Program Files\LLVM\bin`.
- Members use a trailing underscore: `name_`. Do not add new `m_name` members.
- Includes are rooted at `src/`: `#include "kernel/qt/app_data.h"`, never `"app_data.h"`.
- Qt string literals: `QStringLiteral("...")`. Do not introduce `u"..."_s` or `QLatin1String` in new
  code except where an API needs a Latin-1 view.
- `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, `UNICODE` come from the `wabar_options` target; do not redefine
  them locally.
- `.editorconfig` defines whitespace and line endings: CRLF for C++ sources and docs, LF for QML.

## Error handling

Exceptions never cross a Qt boundary: slots, event handlers, timers and any callback Qt invokes.
Every entry point from Qt into our code either uses non-throwing APIs (`json::value`, `find`,
`std::from_chars`, `std::optional` returns) or wraps its body in `try`/`catch` and logs. At API
boundaries return values or `std::optional`; exceptions thrown and caught inside one module are
fine. Never use the `const` `nlohmann::json::operator[]` on a key that may be missing. See
`docs/architecture.md`.

## Live desktop rules

- Tests and tooling always pass `--config-home <dir>` to `wabar.exe`. Never run it against the
  user's real config (`%USERPROFILE%\.config\wabar`) and never write to that folder.
- Tests never touch the user's real bar, wallpaper, tray or Explorer. Use `--no-tray` so a test run
  never hooks Explorer, and never change the wallpaper outside a `--dry-run`.
- A regular start reserves screen work area. `--register-app-bar` forces that for isolated runs,
  `--safe` starts the built-in bar and ignores the config.

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
- Scopes: the unit names `kernel`, `control`, `host`, `ui`, `app`; the feature names `audio`,
  `bluetooth`, `brightness`, `clock`, `control_center`, `github`, `media`, `network`,
  `notifications`, `shell`, `system_stats`, `taskbar`, `tray`, `wallpapers`, `workspaces`; and
  `build`, `docs`, `test`, `style`, `chore`. The scope is optional. `tools/hooks/commit-msg` checks it.
- One logical change per commit; `tools\check.ps1` green before committing.
- Agent-authored commits end with the attribution trailer the session instructions give.
