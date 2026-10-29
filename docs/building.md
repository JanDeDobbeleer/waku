# Building

Prerequisites (Windows 11, ARM64 or x64 host):

- Visual Studio 2022 Build Tools (MSVC 14.44, Windows SDK 10.0.26100 with C++/WinRT headers)
- Qt 6.10.2 from aqtinstall at `C:\Qt\6.10.2\msvc2022_arm64` and `...\msvc2022_64`
  (`QT_ROOT` overrides the `C:\Qt\6.10.2` root)
- vcpkg checkout at `C:\vcpkg` (`git clone https://github.com/microsoft/vcpkg C:\vcpkg`, then
  `C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics`). The Build Tools' bundled vcpkg has no ports
  tree, so it can't resolve the `builtin-baseline` in `vcpkg.json`.

Triplets are `arm64-windows-static-md` / `x64-windows-static-md`: static vcpkg libraries, dynamic
CRT (Qt is dynamic and uses the same CRT), so no extra DLLs to ship.

Run every command in one `cmd.exe` after `vcvarsall.bat` (`arm64`, or `arm64_amd64` to cross-build
x64 from an ARM64 host). `vcvarsall` points `VCPKG_ROOT` at the bundled vcpkg, so set it after.
A `vswhere.exe is not recognized` line from `vcvarsall` is harmless.

```powershell
$vc = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat'

# ARM64 debug / release: configure, build, test
cmd /c "call `"$vc`" arm64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-debug && cmake --build --preset arm64-debug && ctest --preset arm64-debug"
cmd /c "call `"$vc`" arm64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-release && cmake --build --preset arm64-release && ctest --preset arm64-release"

# x64 release, cross-built (tests run under emulation)
cmd /c "call `"$vc`" arm64_amd64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset x64-release && cmake --build --preset x64-release && ctest --preset x64-release"

# Portable folder: dist\msvc2022_arm64 (or msvc2022_64)
cmd /c "call `"$vc`" arm64 && set VCPKG_ROOT=C:\vcpkg&& cmake --build --preset arm64-release --target deploy"
```

On an x64 host use `vcvarsall.bat x64` and the same presets.

Check the deployed build without opening a window:

```powershell
dist\msvc2022_arm64\yasb-native.exe --version   # prints "yasb-native 0.1.0"
dist\msvc2022_arm64\yasb-native.exe --smoke     # builds QApplication, exits 0
```

## App services and CLI

Normal startup owns the `yasb_reborn` single-instance mutex, watches `config.yaml` and `styles.css`
when their `watch_config` / `watch_stylesheet` settings are enabled, and starts the tray and named
pipes. A config change restarts with `--restart-wait`; a stylesheet change applies without restarting.
Logs go to `yasb-native.log` in the selected config directory.

`yasbc-native.exe` sends plain-text commands to `\\.\pipe\yasb_pipe_cli`:

```powershell
build\arm64-debug\yasbc-native.exe reload
build\arm64-debug\yasbc-native.exe show-bar --screen DISPLAY1
build\arm64-debug\yasbc-native.exe stop
```

The command pipe replies `ACK` for `reload`, `stop`, `show-bar`, `hide-bar`, and `toggle-bar`;
other commands receive `CLI Unknown Command`. The separate `\\.\pipe\yasb_pipe_log` emits
NUL-terminated JSON `DATA` frames and answers JSON `PING` messages with `PONG`.
A regular start registers the app bar and reserves the work area when the bar's `windows_app_bar` is true, as Python does. `--screenshot`, `--config-home` and the offscreen platform never register it; `--register-app-bar` forces it and `--no-app-bar` suppresses it.

Render a bar offscreen to a PNG (no window, no app-bar registration; the unpackaged debug exe has no
Qt DLLs, so reuse the ones deployed next to the tests):

```powershell
$t = "$PWD\build\arm64-debug\tests"
$env:PATH = "$t;$env:PATH"; $env:QT_PLUGIN_PATH = $t; $env:QT_QPA_PLATFORM = 'offscreen'
build\arm64-debug\yasb-native.exe --config-home "$PWD\build\cfg" --screenshot "$PWD\build\bar.png"
```

Stat widget factories (CPU, memory, battery) are exposed through
`yasb::widgets::registerStatWidgets()` in `widgets\stat_widgets.h`. `app\main.cpp` calls it once
during startup, before `WidgetBuilder` constructs the configured widgets; repeated calls are no-ops.

## Tray hook DLL

`yasb-native.exe` ships `YASBNativeTrayHook.dll` (x64) or `YASBNativeTrayHook_arm64.dll` next to
it; the systray widget injects it into Explorer to see tray icons. `hook/` builds it with a static
CRT, and each preset builds the DLL for its own architecture, so an ARM64 host runs the ARM64
preset's build. The DLL must match Explorer's architecture: an x64 build running under emulation
on ARM64 cannot use it and has no tray host (the tray stays empty). The pipe and
watchdog mutex names are set once in the root `CMakeLists.txt` (`YASB_TRAY_PIPE_NAME`,
`YASB_TRAY_MUTEX_NAME`) and passed to both the DLL and `platform/`. The DLL detaches from Explorer
by itself (up to 5 s) after the host exits, so wait for it to unload before starting another build
that hooks the tray (the A/B harness does). Automated tests never inject it: they use a private
pipe and fake injector, and the production systray widget creates no tray client on the
`offscreen` platform.

## Checks

`tools\check.ps1` is the definition of done: clang-format check, generated-file drift check,
`arm64-debug` build and `ctest`. It sets up the MSVC environment itself.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1          # everything
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -Fast    # format + generated + static checks
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -SkipFormat -Preset arm64-release
pwsh -NoProfile -File tools\check.ps1 -Analyze                       # local static analysis
```

Configure also writes `build\<preset>\compile_commands.json` for clang tooling. Git hooks live in
`tools\hooks`; enable them with `git config core.hooksPath tools/hooks`.

### Local static analysis

`-Analyze` requires PowerShell 7, the build prerequisites above and LLVM `clang-tidy` on `PATH`
or in `C:\Program Files\LLVM\bin`. It runs the fast checks, then a clean MSVC `/analyze` build
and clang-tidy over production compilation units in `app`, `core`, `platform`, `widgets` and
`tools\yasbc-native`. It does not run tests, launch the bar or register an app-bar.
`-Fast` and `-Staged` cannot be combined with `-Analyze`; hooks still run only fast checks.

Analysis uses `build\<preset>-analyze` with `BUILD_TESTING=OFF` and `YASB_ANALYZE=ON`.
The default `YASB_ANALYZE` is `OFF`; ordinary checks explicitly reset it to `OFF`.
The option applies `/analyze /analyze:external-` only through our `yasb_options` target.
Generated option headers are external for MSVC and excluded by clang-tidy, as are Qt and
third-party headers. Generated MOC units are built by MSVC but are not clang-tidy entry points.
Logs and diagnostic counts appear in the analysis directory and the check summary. Missing tools,
compiler errors and analyzer findings fail the command; an incremental no-op cannot count as analysis.

`.clang-tidy` selects explicit checks rather than wildcard families: suspicious assertions, dangling
handles, erase/size/integer mistakes, invalid forwarding and use-after-move; unsafe C library/thread
APIs; ineffective moves and throwing move constructors; and `nullptr`/`override` consistency.
Naming checks require the `_` suffix on private/protected state and static class members, without
imposing snake_case on existing camelCase_ names. Public aggregate/schema fields retain their names.
This is a baseline, not a proof of thread safety or a whole-program analysis.

The clang-tidy database copy removes MSVC-only `/analyze`, `/analyze:external-` and
`/external:anglebrackets`; dependency `/external:I` paths remain. The driver receives the preset's
ARM64 or x64 target explicitly. Clang's overloaded-virtual warning is disabled because Qt signals
named `event()` intentionally do not override `QObject::event(QEvent*)`; other diagnostics remain
errors. Verified locally with MSVC 19.44, LLVM 22.1.8 and ARM64 Qt 6.10.2: 42 production units.
The x64/Release presets are supported by the command but were not validated in this R11 gate.

Notes:

- Tests are copied next to `yasb_tests.exe` with `windeployqt` (including the `offscreen`
  platform plugin) and `ctest` sets `QT_QPA_PLATFORM=offscreen`.
- `deploy` also drops `vc_redist.<arch>.exe` in `dist` for machines without the VC++ runtime.
- Warnings are errors (`/WX`) for our targets only; vcpkg and Qt code are unaffected, and headers
  included with angle brackets are treated as external (`/external:anglebrackets /external:W0`).
- MSVC only: configuring with another compiler stops with an error.
- Debug builds use `/Z7` because `/ZI` conflicts with `/guard:cf`.
