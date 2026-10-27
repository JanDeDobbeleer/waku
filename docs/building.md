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
`--register-app-bar` remains opt-in; regular startup does not reserve screen work area.

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

## Checks

`tools\check.ps1` is the definition of done: clang-format check, generated-file drift check,
`arm64-debug` build and `ctest`. It sets up the MSVC environment itself.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1          # everything
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -Fast    # format + generated + static checks
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -SkipFormat -Preset arm64-release
```

Configure also writes `build\<preset>\compile_commands.json` for clang tooling. Git hooks live in
`tools\hooks`; enable them with `git config core.hooksPath tools/hooks`.

Notes:

- Tests are copied next to `yasb_tests.exe` with `windeployqt` (including the `offscreen`
  platform plugin) and `ctest` sets `QT_QPA_PLATFORM=offscreen`.
- `deploy` also drops `vc_redist.<arch>.exe` in `dist` for machines without the VC++ runtime.
- Warnings are errors (`/WX`) for our targets only; vcpkg and Qt code are unaffected, and headers
  included with angle brackets are treated as external (`/external:anglebrackets /external:W0`).
- MSVC only: configuring with another compiler stops with an error.
- Debug builds use `/Z7` because `/ZI` conflicts with `/guard:cf`.
