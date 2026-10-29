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
cmd /c "call `"$vc`" arm64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-debug && cmake --build --preset arm64-debug && ctest --preset arm64-debug"
cmd /c "call `"$vc`" arm64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset arm64-release && cmake --build --preset arm64-release && ctest --preset arm64-release"

# x64 release, cross-built (tests run under emulation)
cmd /c "call `"$vc`" arm64_amd64 && set VCPKG_ROOT=C:\vcpkg&& cmake --preset x64-release && cmake --build --preset x64-release && ctest --preset x64-release"
```

On an x64 host use `vcvarsall.bat x64` and the same presets.

Everything lands in `build\<preset>`: `wabar.exe`, `wabarc.exe`, the tray hook DLL next to them and
`wabar_tests.exe` with the Qt DLLs and the offscreen platform plugin that `windeployqt`
copies next to it. The unpackaged debug build runs from that folder. `spike/README.md` explains how
to run the bar and `wabarc`.

## Tray hook DLL

`wabar.exe` ships `YASBNativeTrayHook.dll` (x64) or `YASBNativeTrayHook_arm64.dll` next to it; the
Tray widget injects it into Explorer to see tray icons. `src/features/tray/hook/` builds it with a static CRT, and
each preset builds the DLL for its own architecture, so an ARM64 host runs the ARM64 preset's build.
The DLL must match Explorer's architecture: an x64 build running under emulation on ARM64 cannot use
it and has no tray host (the tray stays empty). The pipe and watchdog mutex names are set once in
the root `CMakeLists.txt` (`YASB_TRAY_PIPE_NAME`, `YASB_TRAY_MUTEX_NAME`) and passed to both the DLL
and `wabar_tray`. The DLL detaches from Explorer by itself (up to 5 s) after the host exits, so wait
for it to unload before starting another run that hooks the tray. Automated tests never inject it:
they use a private pipe and fake injector, and the tray host is not created on the `offscreen`
platform. Pass `--no-tray` to `wabar.exe` for runs that must not hook Explorer.

## Checks

`tools\check.ps1` is the definition of done: clang-format check, include-layering check,
json-subscript check, `arm64-debug` build and `ctest`. It sets up the MSVC environment itself.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1          # everything
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -Fast    # format + layering + json-subscript
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1 -SkipFormat -Preset arm64-release
pwsh -NoProfile -File tools\check.ps1 -Analyze                       # local static analysis
```

Configure also writes `build\<preset>\compile_commands.json` for clang tooling. Git hooks live in
`tools\hooks`; enable them with `git config core.hooksPath tools/hooks`.

### Local static analysis

`-Analyze` requires PowerShell 7, the build prerequisites above and LLVM `clang-tidy` on `PATH`
or in `C:\Program Files\LLVM\bin`. It runs the fast checks, then a clean MSVC `/analyze` build
and clang-tidy over production compilation units in `core`, `platform` and `widgets`. It does not
run tests, launch the bar or register an app-bar. `-Fast` and `-Staged` cannot be combined with
`-Analyze`; hooks still run only fast checks.

Analysis uses `build\<preset>-analyze` with `BUILD_TESTING=OFF` and `YASB_ANALYZE=ON`.
The default `YASB_ANALYZE` is `OFF`; ordinary checks explicitly reset it to `OFF`.
The option applies `/analyze /analyze:external-` only through our `wabar_options` target.
Qt and third-party headers are external for MSVC and excluded by clang-tidy. Generated MOC units
are built by MSVC but are not clang-tidy entry points. Logs and diagnostic counts appear in the
analysis directory and the check summary. Missing tools, compiler errors and analyzer findings fail
the command; an incremental no-op cannot count as analysis.

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
- Warnings are errors (`/WX`) for our targets only; vcpkg and Qt code are unaffected, and headers
  included with angle brackets are treated as external (`/external:anglebrackets /external:W0`).
- MSVC only: configuring with another compiler stops with an error.
- Debug builds use `/Z7` because `/ZI` conflicts with `/guard:cf`.
