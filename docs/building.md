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

Everything lands in `build\<preset>`: `waku.exe`, `wakuc.exe`, `wakukeys.exe`, the tray hook DLL next to them and
`waku_tests.exe` with the Qt DLLs and the offscreen platform plugin that `windeployqt` copies next
to it. The targets are `waku`, `wakuc`, `wakukeys`, `WakuTrayHook` (`WakuTrayHook_arm64` on ARM64),
`waku_tests` and `deploy`; select one with `cmake --build --preset <preset> --target <name>`.

`build\<preset>\qml_modules` holds the build copy of every QML module of `src/` (qmldir, `.qmltypes`,
the `.qml` files), one folder per URI: `qml_modules\waku`, `qml_modules\waku\services`,
`qml_modules\waku\widgets`, `qml_modules\Quickshell\Io` and so on (`QT_QML_OUTPUT_DIRECTORY`, set in
`src/CMakeLists.txt`). qmlcachegen and qmllint get `qml_modules` as import path and so resolve
imports across modules; for an editor, pass it to qmlls or `qmllint -I`. `waku.exe` never reads the
folder: it loads its modules from the static plugins and the resources (`qrc:/qt/qml/<URI>`).
`build\<preset>\qml` is a different folder: Qt's own modules, copied there by `windeployqt`.

## Run

Run `waku` from the build folder. The config home is a folder with `shell.qml` (and optionally
`theme.qml` and `widgets/`). The repository has a sample in `examples/config`. Without a config
home, waku uses `%USERPROFILE%\.config\waku`; while that folder has no `shell.qml` it shows the
built-in bar (`src/app/waku/DefaultShell.qml`) and switches to your `shell.qml` as soon as you
create it.

```powershell
build\arm64-debug\waku.exe --config-home examples\config
build\arm64-debug\wakuc.exe --config-home examples\config status
```

`waku.exe` flags:

| Flag | Effect |
|---|---|
| `--config-home <dir>` | The config home. Default: `$WAKU_CONFIG_HOME`, else `%USERPROFILE%\.config\waku`. Only the default is implicit: a missing `shell.qml` there shows the built-in bar, in a config home you chose it is an error. |
| `--no-app-bar` | Never reserve screen work area. By default windows that ask (`exclusiveZone`, `exclusionMode`, `Panel.exclusive`) reserve it on a live desktop; tests and tools pass this flag. |
| `--safe` | Start the built-in shell with the default theme and ignore the config home. |
| `--import-path <dir>` | Add a folder of QML modules to the import path. Repeatable. See "User-built QML plugins". |
| `--no-keys` | Do not start `wakukeys.exe` beside waku. By default, when the config home has a `keys.conf`, waku starts the key daemon and stops it on exit (a daemon that already runs is left alone). Tests and tools pass this flag. |
| `--no-tray` | Never hook Explorer; the Tray widget stays empty. |
| `--keep-taskbar` | Leave the Windows taskbar alone. By default, on a live desktop, waku hides it (all monitors) while it runs and restores it on exit; tests and tools pass this flag. See "The Windows taskbar" in `docs/architecture.md`. |
| `--no-hotkeys` | Register no global hotkey: `GlobalShortcut` keys stay unregistered and the shortcuts fire only through `wakuc shortcut`. |
| `--no-shell-desktops` | Never use the undocumented shell interface behind Task View for virtual desktops: closing a desktop, creating one without a keystroke, the animated switch and the change notifications. The registry and the documented calls remain, so switching still works and closing is unavailable (`Workspaces.canRemove` is false). See "Shell interface" in `src/features/workspaces/design.md`. |
| `--no-tiling` | Never start the built-in tiler. The tiler is on by default on a live desktop; tests and tools pass this flag. See "The built-in tiler" in `docs/architecture.md`. |
| `--log-file <path>` | Write a JSON Lines diagnostics log to `<path>`: every log message, the start and stop of the run, screen and scale changes and shell reloads. Off by default. Format and how to read it: `docs/logging.md`. |
| `--font-engine <engine>` | `gdi` (default, small) or `directwrite` (honours variable-font axes, tens of MB more memory and about 1,000 more handles). A shell that uses variable fonts needs `directwrite`. `QT_QPA_PLATFORM` wins when it is set. |
| `--help` | Print the options. |

`wakuc.exe` talks to the bar that runs for the same config home (`--config-home <dir>` or
`WAKU_CONFIG_HOME`):

| Command | Effect |
|---|---|
| `wakuc reload` | Reload the config of the running bar. |
| `wakuc status` | Print what the shell is doing: `running; pid=P; windows=N; safe=yes|no; error=...`. |
| `wakuc surface list` | List the windows that have a `name`: `ok: launcher=hidden, osd=shown`. |
| `wakuc surface show`, `hide`, `toggle`, `status <name>` | Show, hide, flip or report the windows with that `name` (all screens). `show` and `toggle` first allow the bar to take the keyboard focus (`AllowSetForegroundWindow`). Example: `examples/launcher`. |
| `wakuc call` | List the `IpcHandler` targets and their functions. |
| `wakuc call <target> <function> [args...]` | Run a function of the `IpcHandler` with that `target`. Arguments are words converted by the declared parameter types (`string`, `int`, `bool`, `real`, `color`); the answer is the return value on one line (`ok` for `void`, JSON for an object or list, `error: ...` on a failure). |
| `wakuc taskbar hide`, `show`, `status` | Hide the Windows taskbar, restore what waku changed (it stays shown until `hide`), or print `ok: taskbar: hidden|visible|autohide-was-on`. |
| `wakuc tile toggle`, `float`, `retile`, `status` | The built-in tiler (on by default; `--no-tiling` turns it off): float the focused window, or tile it again; float it only; place every window of the shown desktop again; print `ok: enabled=...; active=...; managed=...; ...; stacks=N; ...`. |
| `wakuc tile layout <name>` | Set the layout of the focused window's workspace (`mainStack`, `mainStackTop`, `columns`, `rows`, `bsp`, `grid`, `centerMain`), or `next` / `previous` to cycle. |
| `wakuc tile focus next\|previous\|left\|right\|up\|down` | Focus the next or previous window of the workspace (wrapping), or the nearest tiled window in a direction. |
| `wakuc tile move left\|right\|up\|down`, `promote` | Swap the focused window with its neighbour in that direction; give it the main slot. |
| `wakuc tile stack left\|right\|up\|down`, `unstack`, `cycle-stack next\|previous` | Stacking: put the focused window on the stack of the slot in that direction (windows of a stack share one slot, the top one shows); take it out into its own slot; bring the next or previous window of its stack to the top. |
| `wakuc tile resize horizontal\|vertical increase\|decrease` | Grow or shrink the focused window by one step: horizontal moves the main/stack boundary (5% of the width), vertical changes the height of a stacked window. Per workspace, clamped. |
| `wakuc tile flip horizontal\|vertical`, `monocle`, `pause` | Mirror the layout (again undoes it); the focused window fills the work area, the others stay put (again tiles it back); stop and resume placing windows. |
| `wakuc tile close`, `minimize` | Close (`WM_CLOSE`) or minimize the focused window. |
| `wakuc desktop goto <n>`, `next`, `previous`, `status` | Windows virtual desktops: show desktop `n` (counting from 1; never creates one), the next or previous one (no wrap), or print `ok: current=2; count=3`. |
| `wakuc desktop move <n>` | Send the focused window to desktop `n` without showing it; creates nothing, refuses a window of waku. |
| `wakuc quit` | Stop the bar. |
| `wakuc config init [--force]` | Write a starter `shell.qml` (the built-in bar) into the config home. Needs no running bar. Exit 1 if `shell.qml` exists, unless `--force`. |
| `wakuc wallpaper set <file-or-path>` | Set the wallpaper: a full path, or a file name in the wallpaper folder (case does not matter, the extension may be left out). |
| `wakuc wallpaper random`, `next`, `previous` | Pick from the wallpaper folder; `next` and `previous` wrap. |
| `wakuc wallpaper gallery` | Open or close the wallpaper picker. |
| `wakuc shortcut <name>` | Fire the `GlobalShortcut` with that name (its `onPressed`, then `onReleased`). Bind a key to this command in whkd or AutoHotkey for a shortcut with no `keys`. |
| `wakuc shortcut list` | Print the names of the shortcuts the shell declares. A shortcut named `list` cannot be fired. |

The `tile` and `desktop` commands are meant for the key daemon: `examples/config/keys.conf` is a whkd-syntax file that binds all of them (alt+arrows to focus and move, alt+1..8 for desktops) and replaces a tiling window manager plus whkd setup. Its commands start `wakuc`, so `wakuc.exe` must be on `PATH`.

`--dry-run` after `wallpaper` (not `gallery`) plays the animation without setting the wallpaper.

### `wakukeys`: global hotkeys without whkd

`wakukeys.exe` is a hotkey daemon, a third program next to `waku.exe` and `wakuc.exe` (the bar is
not involved). It is a Windows (GUI) program: no console window ever appears, so a login task or
the Startup folder can run it directly. It reads a whkd binding file (`alt + h : wakuc tile focus
left`), registers the keys with `RegisterHotKey` and runs the commands. A copy of your `whkdrc`
loads unchanged. Syntax, actions and decisions: `src/features/keymap/design.md`.

| Command | Effect |
|---|---|
| `wakukeys [--config <file>] [--config-home <dir>] [--log-file <file>]` | Start the daemon. It returns at once and has no window. `--config` defaults to `keys.conf` in the config home (`--config-home`, else `%USERPROFILE%\.config\waku`) if that file exists. One daemon per user; a second start exits with 1. Errors on start (no file, a bad first file) print to the terminal that ran it; otherwise read `--log-file`. |
| `wakukeys reload`, `stop`, `status` | Reload the file now (it also reloads when it changes; a bad file keeps the old bindings), stop the daemon, or print `ok: running; pid=P; bindings=N; conflicts=C; issues=I; paused=no; config=F`. They print to the console of the terminal and exit 0, 1 (error answer), 2 (no daemon running) or 3 (usage). A shell prompt does not wait for a GUI program, so the exit code and the answer reach you only when the output is read: in PowerShell pipe it (`wakukeys status | Out-String`) or use `Start-Process -Wait`; `cmd /c`, `$(...)` and scripts that capture output wait. |

Beside shell commands, a binding can run `waku:shortcut <name>`, `waku:surface show|hide|toggle
<name>`, `waku:call <target> <function> [args]` and `waku:reload` (all sent to the bar, as `wakuc`
does), `waku:keys-reload`, `waku:keys-stop`, and `waku:activate-or-launch <exe-or-title> <command>`
(focus the app's window, else run the command). `--config-home <dir>` selects the bar.

### Moving a waku 1 config to waku 2

waku 2 uses Quickshell's names. A config home with a `bar.qml` and no `shell.qml` loads the fallback shell, whose error pill
names the rename. There are no aliases; edit the file:

| waku 1 | waku 2 |
|---|---|
| file `bar.qml` | file `shell.qml` |
| `Config` | `ShellRoot` (`Scope` groups objects inside it) |
| `Bar` | `Panel` (a preset: one `PanelWindow` per matching screen) |
| `position: Bar.Top` | `edge: Panel.Top` |
| `height` | `thickness` |
| `reserveSpace` | `exclusive` |
| `backdrop: Bar.Blur` | `backdrop: Backdrop.Blur` |
| `layout: BarLayout {}` | `content: PanelLayout {}` |
| `PopupWindow { anchorItem: x }` | `PopupWindow { anchor.item: x }` |
| `Window.window.spec`, `Window.window.monitorHandle` | `QsWindow.window`, `QsWindow.window.monitorHandle` |
| `Process` from `waku.services` (a command run on an interval: `program`, `arguments`, `interval`) | `ShellProcess`, same properties. `Process` is now Quickshell's (`import Quickshell.Io`: `command`, `running`, `stdout`) |
| `wakuc status`: `bars=N` | `windows=N` |

The Quickshell form of a panel is a `PanelWindow` (`anchors`, `margins`, `implicitHeight`,
`exclusiveZone`, `exclusionMode`, `layer`, `keyboardFocus`), one per screen through
`Variants { model: ScreenList.screens }`. `import Quickshell` and `import Quickshell.Wayland` give
the same types under Quickshell's module names.
Exit codes: 0 ok, 1 the bar answered an error, 2 no bar is running, 3 usage error. More on running
the bar: `src/app/README.md`.

## User-built QML plugins

`<config home>\modules` and every `--import-path <dir>` are added to the QML import path, so a shell
can `import MyModule` for a Qt QML plugin built outside this repository. A folder follows the Qt
layout: `modules\MyModule\qmldir`, the plugin DLL named in `qmldir`, and optionally a
`.qmltypes` file. (The `dist\waku\modules` folder that `deploy` writes is a different thing: a
read-only copy of the built-in widgets.)

Build the plugin with the same Qt (major.minor), the same debug or release flavour and the same
processor as the `waku.exe` that loads it. Before a folder is added, waku reads the metadata of
each plugin in it; one built for another Qt, flavour or processor is refused, one line is logged
(`waku: plugin refused: <file> - <reason>`) and the whole folder is left out, so an import from it
then fails with the usual "module is not installed" error. `--safe` adds no folder. Qt's own `Qt6::Qml`
documents how to write a plugin; `tests/plugin/` is a minimal one. A plugin runs as native code:
see `SECURITY.md`.

## Tray hook DLL

`waku.exe` ships `WakuTrayHook.dll` (x64) or `WakuTrayHook_arm64.dll` next to it; the
Tray widget injects it into Explorer to see tray icons. `src/features/tray/hook/` builds it with a static CRT, and
each preset builds the DLL for its own architecture, so an ARM64 host runs the ARM64 preset's build.
The DLL must match Explorer's architecture: an x64 build running under emulation on ARM64 cannot use
it and has no tray host (the tray stays empty). The pipe and watchdog mutex names are set once in
the root `CMakeLists.txt` (`WAKU_TRAY_PIPE_NAME`, `WAKU_TRAY_MUTEX_NAME`) and passed to both the DLL
and `waku_tray`. The DLL detaches from Explorer by itself (up to 5 s) after the host exits, so wait
for it to unload before starting another run that hooks the tray. Automated tests never inject it:
they use a private pipe and fake injector, and the tray host is not created on the `offscreen`
platform. Pass `--no-tray` to `waku.exe` for runs that must not hook Explorer.

## Deploy

`cmake --build --preset <preset> --target deploy` assembles a portable folder, `dist/waku` by
default (`-DWAKU_DIST_DIR=...` changes it; `dist/` is not tracked):

```
dist/waku/
  waku.exe, wakuc.exe, WakuTrayHook[_arm64].dll
  <Qt runtime>                  from windeployqt, scanning src/ and cmake/deploy_imports/ for QML imports
                                (the latter lists QtQuick.Layouts, QtQuick.Effects, Qt.labs.folderlistmodel,
                                QtQuick.Templates and QtQuick.Controls.Basic, which shells written for
                                Quickshell import. Controls.Basic is the only Controls style deployed;
                                a release build adds about 4.2 MB of DLLs and 0.6 MB of QML)
  imageformats/                 qgif, qico, qjpeg, qsvg and qwebp (about 0.35 MB for qwebp). qwebp comes
                                from the Qt Image Formats module (qtimageformats); `deploy` warns at
                                configure time when it is missing
  examples/config/              the sample config home
  modules/waku/                the host QML files, qmldir and .qmltypes (module `waku`)
  modules/waku/widgets/        the built-in widget .qml files, qmldir, .qmltypes (`waku.widgets`)
  modules/waku/services/       qmldir and .qmltypes of the C++ services (`waku.services`)
```

The widgets stay compiled into `waku.exe`; `modules/` is a read-only reference. Copy a widget into
the config home's `widgets/` folder to change it, and lint it with
`qmllint -I <dist>/waku/modules widgets/Clock.qml`. The target never touches an installed or
running copy. Note that a copy of `waku.exe` outside the checkout has no sample config to fall back
on: pass `--config-home`.

## Binary size

Release builds use whole-program optimisation (`/GL`, `/LTCG`, through
`CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`) and `/Gy /Gw /OPT:REF /OPT:ICF` (`cmake/waku_options.cmake`).
`deploy` skips `dxcompiler.dll` and `dxil.dll` (about 32 MB): Qt Quick runs on Direct3D 11 with
precompiled shaders, so nothing loads them. Measured on ARM64: `waku.exe` 6.46 MB to 6.23 MB, the
other binaries about 3% smaller. `/O1` (MinSizeRel) saves a further 0.2 MB and is not used.

## Release and MSIX

`.github/workflows/release.yml` releases from conventional commits, the way oh-my-posh does. A push
to `main` that changes shipped files (`src/`, `cmake/`, `examples/`, `packaging/`, the CMake and
vcpkg files, the release workflow and its scripts) runs it. The `changelog` job reads the commits
since the last `vX.Y.Z` tag: `feat` bumps the minor version, `fix` and `perf` the patch version,
and a `BREAKING CHANGE` footer or `!` the major version. Other types (`ci`, `build`, `docs`,
`chore`, `refactor`, `test`, `style`) release nothing, and a run with no `feat`, `fix`, `perf` or
breaking commit stops there. The new version is the only version input: the workflow passes it to
CMake as `-DWAKU_VERSION` (the default in `CMakeLists.txt` is for local builds) and to
`packaging/msix/build.ps1`. The release notes are the generated changelog. Run the workflow by
hand (`workflow_dispatch`, optionally with a version) to dry-run the packaging: it publishes
nothing. Jobs:

1. `changelog`: the next version, the tag and the notes.
2. `build` (x64 on `windows-latest`, ARM64 on `windows-11-arm`): `release` preset (no tests:
   `build.yml` runs them on every pull request), the `deploy` target, then the C++ runtime DLLs
   from `VCToolsRedistDir` into `dist/waku` (a package cannot run `vc_redist`).
3. `package` (both architectures): `packaging/msix/build.ps1` makes `waku-<arch>.msix` and
   `waku-<arch>.appinstaller`. A release, or a manual run with `sign`, signs `waku.exe`,
   `wakuc.exe`, the tray hook DLLs and then the MSIX with Azure Trusted Signing.
4. `release` (pushes only): creates the tag on the built commit and a GitHub release with both
   MSIX files, both App Installer files, `checksums.txt` and the changelog as notes.

Repository secrets for signing: `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`, `AZURE_TENANT_ID`. The
account and certificate profile are in `packaging/msix/signing.json`; the certificate subject must
equal `Publisher` in `packaging/msix/appxmanifest.xml`. Details: `packaging/msix/README.md`.

To build a package by hand, run `deploy` for the architecture and then
`packaging\msix\build.ps1 -Architecture <x64|arm64> -Version X.Y.Z`. The package holds the whole
portable folder. An installed `waku.exe` with no config shows the built-in bar; `examples/config` is
shipped in the folder as the starting point to copy. The packaged behaviour that tests cannot see is
in `docs/release-checklist.md`.

## Checks

`tools\check.ps1` is the definition of done: clang-format check, structure check
(rules S1-S8), json-subscript check, `arm64-debug` build, `qmllint` and `ctest`. It sets up the MSVC
environment itself. The `qmllint` step builds `all_qmllint` and fails when its warning count rises
above `tools/qmllint-baseline.txt` (lower the file when the count falls; never raise it). The
modules resolve each other through `build\<preset>\qml_modules` (see Build). `all_qmllint` also
runs `waku_examples_qmllint` (`cmake/waku_examples_lint.cmake`), which lints every `.qml` under
`examples/` against the same folder, as a config home would be linted.
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
and clang-tidy over production compilation units under `src`. It does not
run tests, launch the bar or register an app-bar. `-Fast` and `-Staged` cannot be combined with
`-Analyze`; hooks still run only fast checks.

Analysis uses `build\<preset>-analyze` with `BUILD_TESTING=OFF` and `WAKU_ANALYZE=ON`.
The default `WAKU_ANALYZE` is `OFF`; ordinary checks explicitly reset it to `OFF`.
The option applies `/analyze /analyze:external-` only through our `waku_options` target.
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

- Tests are copied next to `waku_tests.exe` with `windeployqt` (including the `offscreen`
  platform plugin) and `ctest` sets `QT_QPA_PLATFORM=offscreen`.
- `ctest` has one test per gtest suite (about 210, read from the `TEST` lines at configure time),
  not one per case (about 1300): a suite runs in one process, and `-j` runs suites in parallel
  (`CTEST_PARALLEL_LEVEL` limits it). `ctest -R Tray` still selects by suite name; run one case
  with `waku_tests --gtest_filter=Suite.Case`. Editing a test source re-runs the configure step
  so a new suite is picked up. `-DWAKU_TEST_PER_CASE=ON` brings back one process per case.
- Warnings are errors (`/WX`) for our targets only; vcpkg and Qt code are unaffected, and headers
  included with angle brackets are treated as external (`/external:anglebrackets /external:W0`).
- MSVC only: configuring with another compiler stops with an error.
- `waku.services` is one QML module built from every feature's `qml/*.{h,cpp}` (one module, so
  widget and service names cannot clash; see `src/modules/qml_modules.cmake`). Its generated type
  registration includes every one of those headers, so a change to any service header recompiles
  `waku_services_qmltyperegistrations.cpp` (12 s on the ARM64 debug build with two jobs) and
  `mocs_compilation.cpp` (15 s), against about 3 s for a service `.cpp`. That is acceptable today;
  if it starts to dominate incremental builds, the services' C++ can move into per-feature static
  libraries with only the registration kept central.
- After adding a QML-facing header an incremental build may skip moc for `waku_services` (AUTOMOC
  misses the new header). Delete `build\<preset>\src\waku_services_autogen\timestamp` and build
  again.
- A feature whose `win32/` code includes C++/WinRT headers can precompile them with
  `waku_feature(... WINRT_PCH winrt/base.h winrt/Windows.Foundation.h ...)`. Measured on the
  ARM64 debug build (two jobs, nine WinRT translation units of audio, media, network and
  notifications): recompiling the nine went from 33 s to 15 s of compile time (24 s to 15 s wall),
  and building the four libraries from scratch from 44 s to 31 s including the four PCHs.
- Debug builds use `/Z7` because `/ZI` conflicts with `/guard:cf`.
- Because qmlcachegen resolves the imports between waku's modules (`build\<preset>\qml_modules`),
  it compiles more bindings and functions of the built-in QML to C++: 1313 instead of 848 in the
  generated `*_qml.cpp` when the folders became URI-shaped. A binding that qmlcachegen cannot type
  still runs in the interpreter, as before.
