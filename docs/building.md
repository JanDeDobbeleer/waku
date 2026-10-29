# Building

Prerequisites (Windows 11, ARM64 or x64 host):

- Visual Studio 2022 Build Tools (MSVC 14.44, Windows SDK 10.0.26100 with C++/WinRT headers)
- Qt 6.10.2 from aqtinstall at `C:\Qt\6.10.2\msvc2022_arm64` and `...\msvc2022_64`
  (`QT_ROOT` overrides the `C:\Qt\6.10.2` root)
- vcpkg checkout at `C:\vcpkg` (`git clone https://github.com/microsoft/vcpkg C:\vcpkg`, then
  `C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics`). The Build Tools' bundled vcpkg has no ports
  tree, so it can't resolve the `builtin-baseline` in `vcpkg.json`.

Triplets are `arm64-windows-static-md` / `x64-windows-static-md`: static vcpkg libraries, dynamic
CRT (Qt is dynamic and uses the same CRT). vcpkg supplies only `nlohmann-json` and `gtest`.

Run every command in one `cmd.exe` after `vcvarsall.bat` (`arm64`, or `arm64_amd64` to cross-build
x64 from an ARM64 host). `vcvarsall` points `VCPKG_ROOT` at the bundled vcpkg, so set it after.
A `vswhere.exe is not recognized` line from `vcvarsall` is harmless.

```powershell
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'

# ARM64 debug / release: configure, build, test
cmd /c "call `"$vc`" arm64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-debug && cmake --build --preset arm64-debug && ctest --preset arm64-debug -j"
cmd /c "call `"$vc`" arm64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-release && cmake --build --preset arm64-release && ctest --preset arm64-release -j"

# x64 release, cross-built (tests run under emulation)
cmd /c "call `"$vc`" arm64_amd64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset x64-release && cmake --build --preset x64-release && ctest --preset x64-release -j"
```

On an x64 host use `vcvarsall.bat x64` and the same presets.

Everything lands in `build\<preset>`: `wabar.exe`, `wabarc.exe`, the tray hook DLL next to them and
`wabar_tests.exe` with the Qt DLLs and the offscreen platform plugin that `windeployqt` copies next
to it. The targets are `wabar`, `wabarc`, `WabarTrayHook` (`WabarTrayHook_arm64` on ARM64),
`wabar_tests` and `deploy`; select one with `cmake --build --preset <preset> --target <name>`.

## Run

Run `wabar` from the build folder. It needs a config home: a folder with `shell.qml` (and optionally
`theme.qml` and `widgets/`). The repository has a sample in `examples/config`.

```powershell
dist\msvc2022_arm64\wabar.exe --version   # prints "wabar 0.1.0"
dist\msvc2022_arm64\wabar.exe --smoke     # builds QApplication, exits 0
```

`wabar.exe` flags:

Normal startup owns the `wabar` single-instance mutex, watches `config.yaml` and `styles.css`
when their `watch_config` / `watch_stylesheet` settings are enabled, and starts the tray and named
pipes. A config change restarts with `--restart-wait`; a stylesheet change applies without restarting.
Logs go to `wabar.log` in the selected config directory.

### Config and data folders

The default config home is `%USERPROFILE%\.config\wabar`; `--config-home` or `WABAR_CONFIG_HOME`
overrides it. The data folder is `%LOCALAPPDATA%\wabar` (pinned apps, GitHub token, tray state,
wallpaper thumbnails). Both moved from the Python YASB names. The path logic is in
`platform/app_data.h`; its tests use temp folders only.

On a regular start (no `--config-home`), wabar copies the old files once and logs one line naming
what it copied. It never writes to, moves or deletes anything in the old folders.

- Config: if `.config\wabar\config.yaml` is missing and `.config\yasb\config.yaml` exists, it
  copies `config.yaml` and `styles.css` (only those two, when present). Skipped when
  `WABAR_CONFIG_HOME` is set.
- Data: unless `%LOCALAPPDATA%\wabar\migrated_from_yasb` exists, it copies the files directly
  inside `%LOCALAPPDATA%\YASB` that the new folder lacks, then writes that marker file. Delete
  the marker to copy again. Thumbnail caches are not copied.

`wabarc.exe` sends plain-text commands to `\\.\pipe\yasb_pipe_cli`:

```powershell
build\arm64-debug\wabarc.exe reload
build\arm64-debug\wabarc.exe show-bar --screen DISPLAY1
build\arm64-debug\wabarc.exe stop
```

`--dry-run` after `wallpaper` (not `gallery`) plays the animation without setting the wallpaper.

### Moving a wabar 1 config to wabar 2

```powershell
$t = "$PWD\build\arm64-debug\tests"
$env:PATH = "$t;$env:PATH"; $env:QT_PLUGIN_PATH = $t; $env:QT_QPA_PLATFORM = 'offscreen'
build\arm64-debug\wabar.exe --config-home "$PWD\build\cfg" --screenshot "$PWD\build\bar.png"
```

Stat widget factories (CPU, memory, battery) are exposed through
`wabar::widgets::registerStatWidgets()` in `widgets\stat_widgets.h`. `app\main.cpp` calls it once
during startup, before `WidgetBuilder` constructs the configured widgets; repeated calls are no-ops.

## Native taskbar

By default wabar hides the Windows taskbar at start and restores it at exit (config key
`hide_windows_taskbar`, default `true`, wabar only). `--keep-taskbar` turns it off for one run.
The offscreen platform, `--screenshot` and `--config-home` never hide it; `--hide-taskbar` allows it
with `--config-home`. Details: [taskbar-hiding.md](taskbar-hiding.md).

## Tray hook DLL

`wabar.exe` ships `WabarTrayHook.dll` (x64) or `WabarTrayHook_arm64.dll` next to
it; the systray widget injects it into Explorer to see tray icons. `hook/` builds it with a static
CRT, and each preset builds the DLL for its own architecture, so an ARM64 host runs the ARM64
preset's build. The DLL must match Explorer's architecture: an x64 build running under emulation
on ARM64 cannot use it and falls back to the legacy tray window, as Python YASB does. The pipe and
watchdog mutex names are set once in the root `CMakeLists.txt` (`WABAR_TRAY_PIPE_NAME`,
`WABAR_TRAY_MUTEX_NAME`) and passed to both the DLL and `platform/`. The DLL detaches from Explorer
by itself (up to 5 s) after the host exits, so wait for it to unload before starting another build
that hooks the tray (the A/B harness does). Automated tests never inject it: they use a private
pipe and fake injector, and the production systray widget creates no tray client on the
`offscreen` platform.

## Checks

`tools\check.ps1` is the definition of done: clang-format check, structure check
(rules S1-S8), json-subscript check, `arm64-debug` build, `qmllint` and `ctest`. It sets up the MSVC
environment itself. The `qmllint` step builds `all_qmllint` and fails when its warning count rises
above `tools/qmllint-baseline.txt`. Most of today's warnings are unresolved `wabar`,
`wabar.services` and `Quickshell` imports: the module output folders are not URI-shaped (see
`src/modules/qml_modules.cmake`), so qmllint cannot see the types and also reports their uses as
`unqualified` or `missing-property`. Making the folders URI-shaped is a codegen change of its own;
after it the baseline should drop sharply. The examples (`examples/*/shell.qml`) are not linted
yet for the same reason.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1          # everything
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -Fast    # format + structure + json-subscript
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -Fast -Staged  # the same on staged files only
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -SkipFormat -Preset arm64-release
pwsh -NoProfile -File tools\check.ps1 -Analyze                       # local static analysis
```

Configure also writes `build\<preset>\compile_commands.json` for clang tooling.

### Git hooks

The hooks in `tools\hooks` are `pre-commit` (runs `check.ps1 -Fast -Staged`) and `commit-msg`
(checks the conventional-commit subject and its scope; the list is in `AGENTS.md`). Enable them once
per clone, together with the blame setting:

```powershell
git config core.hooksPath tools/hooks
git config blame.ignoreRevsFile .git-blame-ignore-revs
```

### Local static analysis

`-Analyze` requires PowerShell 7, the build prerequisites above and LLVM `clang-tidy` on `PATH`
or in `C:\Program Files\LLVM\bin`. It runs the fast checks, then a clean MSVC `/analyze` build
and clang-tidy over production compilation units in `app`, `core`, `platform`, `widgets` and
`tools\wabarc`. It does not run tests, launch the bar or register an app-bar.
`-Fast` and `-Staged` cannot be combined with `-Analyze`; hooks still run only fast checks.

Analysis uses `build\<preset>-analyze` with `BUILD_TESTING=OFF` and `WABAR_ANALYZE=ON`.
The default `WABAR_ANALYZE` is `OFF`; ordinary checks explicitly reset it to `OFF`.
The option applies `/analyze /analyze:external-` only through our `wabar_options` target.
Generated option headers are external for MSVC and excluded by clang-tidy, as are Qt and
third-party headers. Generated MOC units are built by MSVC but are not clang-tidy entry points.
Logs and diagnostic counts appear in the analysis directory and the check summary. Missing tools,
compiler errors and analyzer findings fail the command; an incremental no-op cannot count as analysis.

`.clang-tidy` selects explicit checks rather than wildcard families: suspicious assertions, dangling
handles, erase/size/integer mistakes, invalid forwarding and use-after-move; unsafe C library/thread
APIs; ineffective moves and throwing move constructors; and `nullptr`/`override` consistency.
Naming checks require the `_` suffix on private/protected state and static class members, without
imposing snake_case on existing camelCase_ names. Public aggregate fields retain their names.
This is a baseline, not a proof of thread safety or a whole-program analysis.

The clang-tidy database copy removes MSVC-only `/analyze`, `/analyze:external-` and
`/external:anglebrackets`; dependency `/external:I` paths remain. The driver receives the preset's
ARM64 or x64 target explicitly. Clang's overloaded-virtual warning is disabled because Qt signals
named `event()` intentionally do not override `QObject::event(QEvent*)`; other diagnostics remain
errors. The x64/Release presets are supported by the command but were not validated.

Notes:

- Tests are copied next to `wabar_tests.exe` with `windeployqt` (including the `offscreen`
  platform plugin) and `ctest` sets `QT_QPA_PLATFORM=offscreen`.
- `ctest` has one test per gtest suite (about 210, read from the `TEST` lines at configure time),
  not one per case (about 1300): a suite runs in one process, and `-j` runs suites in parallel
  (`CTEST_PARALLEL_LEVEL` limits it). `ctest -R Tray` still selects by suite name; run one case
  with `wabar_tests --gtest_filter=Suite.Case`. Editing a test source re-runs the configure step
  so a new suite is picked up. `-DWABAR_TEST_PER_CASE=ON` brings back one process per case.
- Warnings are errors (`/WX`) for our targets only; vcpkg and Qt code are unaffected, and headers
  included with angle brackets are treated as external (`/external:anglebrackets /external:W0`).
- MSVC only: configuring with another compiler stops with an error.
- `wabar.services` is one QML module built from every feature's `qml/*.{h,cpp}` (one module, so
  widget and service names cannot clash; see `src/modules/qml_modules.cmake`). Its generated type
  registration includes every one of those headers, so a change to any service header recompiles
  `wabar_services_qmltyperegistrations.cpp` (12 s on the ARM64 debug build with two jobs) and
  `mocs_compilation.cpp` (15 s), against about 3 s for a service `.cpp`. That is acceptable today;
  if it starts to dominate incremental builds, the services' C++ can move into per-feature static
  libraries with only the registration kept central.
- After adding a QML-facing header an incremental build may skip moc for `wabar_services` (AUTOMOC
  misses the new header). Delete `build\<preset>\src\wabar_services_autogen\timestamp` and build
  again.
- A feature whose `win32/` code includes C++/WinRT headers can precompile them with
  `wabar_feature(... WINRT_PCH winrt/base.h winrt/Windows.Foundation.h ...)`. Measured on the
  ARM64 debug build (two jobs, nine WinRT translation units of audio, media, network and
  notifications): recompiling the nine went from 33 s to 15 s of compile time (24 s to 15 s wall),
  and building the four libraries from scratch from 44 s to 31 s including the four PCHs.
- Debug builds use `/Z7` because `/ZI` conflicts with `/guard:cf`.
