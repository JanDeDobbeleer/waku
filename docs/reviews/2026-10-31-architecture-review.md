# Architecture review, 2026-10-31

Scope: branch `perf/lazy-surfaces` at `9f9dcd5` (204 commits ahead of `main`). Read-only: no build,
no test run. Sources: `AGENTS.md`, `docs/architecture.md`, `docs/building.md`, `src/app/README.md`,
`docs/plans/2026-10-30-handoff.md`, `tools/check.ps1`, the CMake files, every `design.md`, and the
code reached by grep and spot reads.

## Verdict

The layout is sound and the rules that keep it so are enforced by `tools/check.ps1` and
`wabar_feature()`: includes point one way (no `host` file includes `features/`, features reach `host`
only through `host/testing/` from tests), the kernel passes its own admission test (every file has
two or more users), no `m_` members, one TODO, every allowlist path exists, CI builds three presets.
The weak spot is the Quickshell compatibility layer: its pieces sit in six places (`src/host/qml`,
`src/host/quickshell`, `src/app/wabar/quickshell_types_*.h`, `src/modules/qml_modules.cmake`,
`src/features/quickshell_io`, and the `media`, `notifications`, `apps` features), and a host target
compiles an `app/` header, which inverts the dependency direction behind rule S2's back. The second
weak spot is drift between `docs/architecture.md` and the code: nine QML singletons and two threads
have no row, the target table predates the seven extra Quickshell modules, and `building.md` misses
a flag. Tests are broad (1169 cases in 97 files) but 88 fixed `qWait(N)` calls and one ctest entry
per case make the suite slower and flakier than it needs to be, and nothing lints the 73 QML files.
The rest is naming debt from wabar 1 and a few small misplacements. Nothing found needs a redesign.

## Findings

Severity: high (breaks a stated rule or costs every contributor time), medium (costs time or hides
risk), low (tidiness). Effort: S (under 2 h), M (half a day to a day), L (several days).
"Parallel" says whether the fix can run beside the others without file conflicts.

### A1. The Quickshell compatibility layer is spread over six places and one host target compiles an `app/` header

Severity: high. Effort: L. Parallel: no (run last, after A10, A11 and A15 are merged).

Where:

- `src/host/quickshell/CMakeLists.txt` lines 31-35: `wabar_quickshell` compiles
  `${PROJECT_SOURCE_DIR}/src/app/wabar/quickshell_types_core.h` and adds `src/app/wabar` to its
  include path.
- `src/app/wabar/quickshell_types_core.h` includes `features/apps/qml/desktop_entry.h` and
  `features/quickshell_io/qml/quickshell_singleton.h`.
- `src/modules/qml_modules.cmake` lines 98-123: three more module targets (`Quickshell.Io`,
  `Quickshell.Services.Mpris`, `Quickshell.Services.Notifications`) built from
  `src/app/wabar/quickshell_types_{io,mpris,notifications}.h`.
- `src/host/quickshell/{wayland,widgets,hyprland,upower,pipewire,systemtray,bluetooth}/`: seven
  modules, QML plus two C++ files.
- `src/host/qml/{script_model,system_clock,persistent_properties,ipc_handler,transform_watcher,shell_config}.*`:
  the C++ behind `ScriptModel`, `SystemClock`, `PersistentProperties`, `IpcHandler`,
  `TransformWatcher`, `Singleton`.
- `src/features/quickshell_io/`, `src/features/media/qml/mpris.*`,
  `src/features/notifications/qml/notification_feed.*`, `src/features/apps/qml/desktop_entry.*`:
  the implementations of `Process`, `FileView`, the `Quickshell` singleton, `Mpris`,
  `NotificationServer`, `DesktopEntry`.

Problem: a newcomer who wants to know "where does Quickshell's `X` live" has no single place to
look; `docs/architecture.md` needs five table rows to answer it. The layering is bent: `host`
(`wabar_quickshell`) depends on `app/` sources and, through them, on two features. S2 does not
catch it because `app/` is exempt and the include is done by CMake. The link dependencies are
under-declared as a result: `wabar_quickshell` registers `wabar::apps::DesktopEntry` and
`wabar::quickshell_io::ElapsedTimer` (their `staticMetaObject` lives in `wabar_services`) but
links only `wabar_host`; the three service modules link `wabar_kernel` only. Both build because
the executables link everything. The comment in `src/host/quickshell/CMakeLists.txt` ("the names
resolve to the same types") is not true for `PopupWindow` and `Variants`: `src/host/quickshell/qml/PopupWindow.qml`
and `Variants.qml` are wrapper components, so `import Quickshell` and `import wabar` give two
distinct QML types there.

Fix: a unit `src/quickshell/` between `features` and `app` (dependency table row: may include
`host/qml/*`, the root and `qml/` headers of any feature, `control/`; only `app/` and `tests/` may
include it). Inside: `core/` (today's `src/host/quickshell/qml` plus `quickshell_types_core.h`),
`wayland/`, `io/`, `widgets/`, `hyprland/`, `services/{mpris,notifications,upower,pipewire,systemtray}/`,
`bluetooth/`, each with its own `CMakeLists.txt`, and a `README.md` with one table: Quickshell
type, module, wabar implementation file. Move `wabar_quickshell_service_module()` out of
`src/modules/qml_modules.cmake`. Link every module target to `wabar_services`. Register the
unit in `tools/check.ps1` (S1, S2, S3, S4) and in the structure section of `AGENTS.md`. Document
the two wrapper types as the exception. The C++ of the core types stays in `src/host/qml/` (it is
the engine); only registrations and QML move.

### A2. Nine QML singletons have no row in the singleton table

Severity: high (the table states "No new singleton without a row here"). Effort: S. Parallel: yes
(one agent owns `docs/architecture.md` for A2, A3, A4, A24).

Missing (all `QML_SINGLETON`):

| Singleton | File |
|---|---|
| `wabar.services.Workspaces` | `src/features/workspaces/qml/workspaces_service.h` |
| `wabar.services.Komorebi` | `src/features/workspaces/qml/komorebi_client.h` |
| `wabar.services.GlazeWM` | `src/features/workspaces/qml/glazewm_client.h` |
| `wabar.services.Media` | `src/features/media/qml/media_service.h` |
| `wabar.services.AudioOutput`, `AudioInput` | `src/features/audio/qml/audio_service.h` |
| `wabar.services.Shell` | `src/features/shell/qml/shell_service.h` |
| `wabar.Theme` | `src/host/qml/bar_theme.h` |
| `wabar.PopupPlacement` | `src/host/qml/popup_placement_qml.h` |

Fix: one row each (owner thread, what it holds, how it stops, live-desktop behaviour), in the
format of the existing rows.

### A3. Two threads are missing from the threading table

Severity: medium. Effort: S. Parallel: yes (same agent as A2).

- `src/kernel/win32/shell_launch.cpp` line 17: `shellOpen` starts a detached `std::thread` per
  call (STA, `ShellExecuteW`). Not in the table, no stop, no owner. Add the row, or run it on the
  global `QThreadPool` so it has the same lifetime story as the other one-shot tasks.
- `src/features/brightness/win32/brightness_win.cpp` lines 658-668: `policyThread_` owns a
  message-only window and the power-setting registration. The `ControlCenter` singleton row names
  it ("the power-setting window thread"); the threading table row for `BrightnessControl` does not.
  Add it to that row (owner, `WM_CLOSE` then `join`).

### A4. The target table and the kernel user lists are stale

Severity: medium. Effort: S. Parallel: yes (same agent as A2).

- `docs/architecture.md` line 22: the `wabar_quickshell` row says "two QML wrappers" and names
  only `Quickshell` and `Quickshell.Wayland`. Missing targets: `wabar_quickshell_{widgets,hyprland,upower,pipewire,systemtray,bluetooth}`
  (`src/host/quickshell/*/CMakeLists.txt`), `wabar_quickshell_services` and the three module
  targets of `src/modules/qml_modules.cmake` lines 98-123, `wabar_test_module`
  (`tests/plugin/CMakeLists.txt`).
- Kernel table, `win32/app_activation.*` users: add `src/features/apps/win32/launch_win.cpp`.
- Kernel table, `qt/app_data.*` users: add apps frecency (`apps_service.cpp`), `color_scheme`
  image stats, `quickshell_io` state/cache/data dirs (`quickshell_singleton.cpp`), host thumbnails
  (`src/host/qml/thumbnails.cpp`).
- "Five QML modules, plus the Quickshell compatibility modules" (line 121): there are 14 modules;
  say so and list them once.

Fix: rewrite those rows against the CMake files; A1 changes them again, so do this first and keep
the rows short.

### A5. User-facing docs have gaps and one stub

Severity: medium. Effort: S. Parallel: yes (files: `docs/building.md`, `README.md`,
`docs/plans/README.md`, `docs/styling.md`, `docs/shells.md`, `src/app/README.md`).

- `docs/building.md` flag table lacks `--font-engine` (`src/app/wabar/main.cpp` line 57; the
  handoff section 7 says a shell with variable fonts needs it).
- `README.md` lists the `wabarc` commands without `call`, `shortcut`, `taskbar`.
- `docs/plans/README.md` has no row for `2026-10-29-wabar-extensibility-proposal.md` (941 lines,
  the document that decided the Quickshell names) or `2026-10-29-wabar-styling-proposal.md`.
- `docs/styling.md` is a three-line stub and `README.md` links to it as "How the look is
  configured".
- `docs/shells.md` (notes for shell authors: masks, shortcuts) is linked from nowhere.

Fix: add the flag and commands; add the two plan rows; either write `docs/styling.md` from the
styling proposal's decided parts or remove the link until it exists; link `docs/shells.md` from
`README.md` and `src/app/README.md`.

### A6. Undocumented-API allowlist: three small gaps in an otherwise exact list

Severity: medium. Effort: S. Parallel: yes (files: `src/features/taskbar/win32/`,
`src/features/brightness/win32/brightness_win.cpp`, one table row).

Checked: all 18 rows name files that exist; every `GetProcAddress` site in `src/` is covered
except the ones below; the hand-laid GUIDs in `audio`, `bluetooth`, `control_center`,
`notifications` (`dnd_win.cpp`) and `taskbar` (`app_identity_win.cpp`) have rows.

- `src/features/taskbar/win32/window_system_win.cpp` lines 321 and 343: `user32!EndTask` by
  `GetProcAddress`. The API is documented, so no allowlist row is needed, but the comment should
  say so in the allowlist's words ("documented, loaded by name because the import library lacks
  it") so the next audit does not stop here.
- `src/features/taskbar/win32/pin_support_win.cpp` line 28 spells out `PKEY_AppUserModel_ID` a
  second time; the allowlist row names only `app_identity_win.cpp`. Share one constant in a
  `taskbar/win32/` header and name both files in the row.
- `src/features/brightness/win32/brightness_win.cpp` lines 41-44 spell out `GUID_VIDEO_SUBGROUP`
  and `GUID_DEVICE_POWER_POLICY_VIDEO_BRIGHTNESS`, which the SDK defines. Use the SDK names.

### A7. One ctest entry per gtest case: 1169 process starts per run

Severity: medium. Effort: M. Parallel: yes (files: `tests/CMakeLists.txt`, `tools/check.ps1`,
`AGENTS.md`, `docs/building.md`).

`tests/CMakeLists.txt` line 54 uses `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)`. Every
case becomes a ctest test and a process that builds a `QGuiApplication` on the offscreen platform
(`tests/main.cpp`), and discovery runs again at every `ctest` call. The documented commands and
`check.ps1` run `ctest` without `-j`.

Fix, in order of cost: add `-j` (number of cores) to the four documented `ctest` lines and to
`check.ps1`; then register one ctest test per suite prefix generated from the `TESTS` list
(`add_test(NAME Tray COMMAND wabar_tests --gtest_filter=Tray*)`), which keeps `ctest -R Tray`
working and cuts the process count from 1169 to about 60. Keep `--gtest_filter` for single cases.

### A8. Fixed sleeps in tests

Severity: high (the handoff records a flake under load). Effort: M. Parallel: yes, one agent per
file group.

88 `QTest::qWait(<fixed>)` calls across the tracked tests (14 of 50 ms, 13 of 100, 12 of 300,
6 of 400, 2 of 700, 1 of 900, 1 of 600, 2 of 500) against 110 `qWaitFor`. The costly and fragile
ones: `src/host/tests/test_shell_host.cpp` lines 68, 563, 565 (700-900 ms to prove a debounce
fired once), `tests/integration/test_safe_mode.cpp` line 97 (500 ms "more than the debounce"),
`src/host/tests/test_popups.cpp` lines 299, 479, `src/host/tests/test_overlays.cpp` line 213,
`src/kernel/tests/test_polled_source.cpp` lines 34, 58, `src/features/wallpapers/tests/test_wallpaper_transition.cpp`
and `test_wallpaper_service.cpp` line 270. Known flake: `WallpaperHandler.ThePipeCarriesTheQuotedLineFromWabarc`
(`src/host/tests/test_wallpaper_handler.cpp`, a real `wabarc.exe` round trip).

Fix: for "nothing more happens" assertions, expose the debounce as a constant or a settable
interval on the class (`ShellHost`, `PolledSource`, the file watcher) and wait that plus a small
margin; for positive waits, `qWaitFor` on the condition. For the pipe test, wait on the server's
`received` signal instead of a timeout. Budget: the suite should lose several seconds of pure
sleeping.

### A9. No QML lint in the definition of done

Severity: medium. Effort: S to M. Parallel: yes (file: `tools/check.ps1`; same agent as A7
because both edit it).

`tools/check.ps1` has no `qmllint` step; 73 `.qml` files under `src/` and 6 under `examples/` are
checked only by compiling in tests. `qt_add_qml_module` already creates `<target>_qmllint`
targets.

Fix: a `qmllint` step between `structure` and `build` (or after `build`, since it needs the
configured tree): `cmake --build --preset arm64-debug --target all_qmllint`, failing on warnings
for `src/`, and a separate pass over `examples/*/shell.qml` with `-I build/.../src/modules`.
Document it in `AGENTS.md` under "Definition of done".

### A10. Names left over from wabar 1, and a host/feature name clash

Severity: medium. Effort: S to M. Parallel: yes for the host files, but serial with A15 (same
headers).

- `src/host/qml/bar_theme.{h,cpp}` holds `ThemeValues`, `ThemeSpec`, `Theme`; `Bar` was deleted in
  2.0. Rename to `theme.{h,cpp}`.
- `src/host/qml/shell_config.h` holds `Scope`, `ShellRoot`, `Singleton`; `Config` was deleted in
  2.0. Rename to `scope.h`.
- `src/host/taskbar_control.{h,cpp}`, `src/host/win32/taskbar_control.cpp`,
  `src/host/qml/taskbar_command.{h,cpp}`, `src/host/testing/taskbar_fakes.h`,
  `src/host/tests/test_taskbar_control.cpp` are about hiding the Windows taskbar;
  `src/features/taskbar/` is wabar's own taskbar widget. `ctest -R Taskbar` selects both. Rename
  the host side to `windows_taskbar_*` (class `WindowsTaskbar`, test suite `WindowsTaskbar`).
- 32 commits on the branch carry the scope `bar`, which `tools/hooks/commit-msg` refuses today.
  History only; mention it in the handoff for the squash to `main`.

Fix: `git mv`, update `CMakeLists.txt`, `quickshell_types.h`, includes, `docs/architecture.md`
paths.

### A11. Feature `shell` overlaps `quickshell_io` and its name collides with the shell vocabulary

Severity: medium. Effort: M. Parallel: yes (files: `src/features/shell/`,
`src/features/quickshell_io/`, `src/features/CMakeLists.txt`, `tools/hooks/commit-msg`,
`AGENTS.md`), but not beside A1.

`src/features/shell/qml/child_process.*` (`ShellProcess`: run a command, interval, timeout) and
`src/features/quickshell_io/qml/qs_process.*` plus `command_spec.*`, `process_job.h`,
`win32/process_job_win.cpp` (`Process`: job object, kill tree) are two process runners with
different kill semantics. "shell" also means `shell.qml`, `ShellHost`, `ShellRoot`, `SafeShell`
and the `Shell` singleton; grep for it returns everything.

Fix: fold `shell` into `quickshell_io` and build `ShellProcess` over the same job-object runner
(one kill-tree implementation), or rename `shell` to `launch` if the two runners must stay apart.
Update the dependency table (`audio`, `bluetooth`, `network`, `github` depend on it), the commit
scopes, and `design.md`.

### A12. The `wallpaper` verb is hard-coded in `control` and `host`

Severity: low. Effort: M. Parallel: yes (files: `src/control/wallpaper_control.h`,
`src/host/qml/control_handler.*`, `src/host/tests/test_wallpaper_handler.cpp`,
`src/features/wallpapers/qml/wallpapers_service.*`, `src/app/wabarc/main.cpp`).

`src/control/wallpaper_control.h` and `host/qml/control_handler.h` lines 19-21, 42-44 carry a
feature's vocabulary (set, random, next, previous, gallery, dry run) in two generic units. The
generic path already exists: `IpcHandler` and `wabarc call <target> <function>`.

Fix: the wallpapers service declares an `IpcHandler`-equivalent target `wallpaper` from C++ (or the
host offers a C++ registration API next to the QML one); `wabarc wallpaper ...` becomes a
client-side alias for `wabarc call wallpaper ...`; `WallpaperControl` and `answerWallpaperCommand`
go. `control` is then protocol only.

### A13. Host types with no consumer in the repository

Severity: low. Effort: S. Parallel: yes.

`wabar.Thumbnails` (`src/host/qml/thumbnails.*`, `src/host/thumbnail_store.*`, two thread pools)
and `WindowThumbnail` (`src/host/qml/window_thumbnail.*`, `src/host/thumbnail.*`,
`src/host/win32/thumbnail_win.cpp`) are used by no `.qml` under `src/` or `examples/`; they
exist for the external caelestia port. They are generic, so they may stay, but nothing in the
tree shows how they are meant to be used and nothing but unit tests exercises their QML face.

Fix: one small example (`examples/gallery/shell.qml`: a `FloatingWindow` with an `image://thumb`
grid and a `WindowThumbnail` of the foreground window) loaded by an integration test, and a line
in `docs/architecture.md` saying which engine services exist for ports rather than for the
built-in widgets.

### A14. `src/host/` is one flat folder for eight concerns

Severity: low. Effort: M. Parallel: no (touches most host paths; skip if A1 and A15 land).

`wabar_host_base` mixes the app bar, hotkeys, the mask poller, the plugin guard, region math,
thumbnails, the Windows taskbar and the DWM backdrop; with `qml/` (44 files) and `tests/` (23)
the unit holds 129 files. It is correct, but a reader has no sub-structure to follow.

Fix, if wanted: sub-folders by concern (`host/window/` for app bar, panel geometry, popup
placement, screen match, region math, mask poller, backdrop; `host/input/` for hotkeys;
`host/plugins/`; `host/thumbnails/`; `host/windows_taskbar/`), each with `win32/` beside it.
Rules S1-S8 do not care. Lower priority than everything above.

### A15. `shell_window.h` is 564 lines and nine classes

Severity: medium (compile time). Effort: M. Parallel: serial with A10 (same files).

`src/host/qml/shell_window.h` declares `ProxiedWindow`, `PanelAnchors`, `PanelMargins`,
`SurfaceFormat`, `QsWindow`, `QsWindowAttached`, `PanelWindow`, `FloatingWindow`, `WlrLayershell`.
It is included by `shell_host.h`, `quickshell_types.h`, `wayland_types.h`, `shell_fixtures.h`
and, through them, by the host module, every Quickshell module and most host and feature tests.
Any edit rebuilds all of them.

Fix: split into `qs_window.h` (base, attached, anchors, margins, surface format),
`panel_window.h`, `floating_window.h`, `wlr_layershell.h`; `shell_host.h` forward-declares.

### A16. `wabar.services` is one registration unit over 40 headers

Severity: low. Effort: S (document) or L (restructure). Parallel: yes.

`src/modules/qml_modules.cmake` builds one module from every feature's `qml/*.h`. The generated
`wabar_services_qmltyperegistrations.cpp` includes all of them, so one changed service header
recompiles it, the plugin and the executables' link. The handoff section 7 notes an AUTOMOC
timestamp miss after adding a header. The reason for one module (name clash between widgets and
services) is documented and valid.

Fix: keep the module; move the moc-timestamp workaround from the handoff into
`docs/building.md` "Notes"; measure the registration TU once and, if it dominates incremental
builds, split the C++ into per-feature static libraries that the module only links
(`qt_add_qml_module` with `NO_PLUGIN_OPTIONAL` sources is not needed; only the registration stays
central).

### A17. WinRT translation units and one 1649-line file

Severity: medium (build time). Effort: M. Parallel: yes (files: `cmake/wabar_feature.cmake`,
`src/features/bluetooth/win32/`, feature `CMakeLists.txt` of the WinRT users).

Twelve `.cpp` files include `<winrt/...>` (audio 2, bluetooth 1, brightness 1, media 2, network 3,
notifications 2, kernel 1). Each one parses the C++/WinRT projection headers from scratch. No
precompiled header, no unity build (`cmake/wabar_options.cmake` sets none).
`src/features/bluetooth/win32/bluetooth_win.cpp` is 1649 lines and `brightness_win.cpp` 778.

Fix: a `WINRT_PCH` option on `wabar_feature()` that applies
`target_precompile_headers(wabar_<f> PRIVATE <winrt/base.h> <winrt/Windows.Foundation.h>)` to the
`WIN32_SOURCES`; split `bluetooth_win.cpp` into radio, devices and battery, audio connection,
pairing. Measure before and after with `-d stats` on the Ninja log.

### A18. Untracked `.claude/` and six stale worktrees

Severity: low. Effort: S. Parallel: yes.

`.gitignore` lacks `/.claude/` (git status shows `?? .claude/`). `git worktree list` has four
`.claude/worktrees/agent-*` entries (three locked) whose branches are merged, plus
`D:/wabar-wt/crash` and `D:/wabar-wt/thumbs` on commits already in this branch. Every repository
grep returns their copies too. The handoff section 2 asks for the prune.

Fix: `git worktree remove` (unlock first), `git branch -d`, add `/.claude/` to `.gitignore`.

### A19. One duplicated fake

Severity: low. Effort: S. Parallel: yes.

`FakeBackend : DesktopWallpaperBackend` exists in
`src/features/wallpapers/testing/fake_wallpaper_backend.h` line 24 and again in
`src/features/wallpapers/tests/test_wallpaper_manager.cpp` line 61. The other repeated names
(`FakeSource` in media, network, notifications, system_stats; `FakeBackend` in bluetooth) are
different types in different namespaces and fine.

Fix: delete the local copy; use the `testing/` one.

### A20. Risky areas with no test of any kind

Severity: medium. Effort: S (checklist) to M (tests). Parallel: yes.

By design the Win32 implementations run only live. Some have no contract test through a fake and
no live record either: `src/features/taskbar/win32/pin_support_win.cpp` (627 lines, pins and
`shell:::{...}` verbs), `src/features/tray/hook/trayhook.cpp` (only the pipe layout is tested),
`src/features/wallpapers/win32/workerw.cpp` (allowlist: "not exercised live on build 26300"),
`src/host/win32/shell_host_win32.cpp` (`NativeHooks`), the `ABM_QUERYPOS`/`ABM_SETPOS` path of
`src/host/win32/app_bar.cpp`. The handoff section 7 lists the live-unverified items but no doc
in `docs/` owns that list.

Fix: `docs/release-checklist.md`: one line per live-only behaviour with the command to exercise it
and the date last seen working; the allowlist rows marked "not exercised live" link to it. Where a
pure part can be carved out (the pin id parser, the WorkerW search order), a unit test.

### A21. The handoff is out of order and its numbers are stale

Severity: low. Effort: S. Parallel: yes (file: `docs/plans/2026-10-30-handoff.md`).

Sections run 1, 2, 3, 4, 4a, 4b, 5, 6, 7, 4c. Section 2 says "144 commits ahead"; it is 204.
Section 4 item 1 and 4b both describe the last full check with different heads.

Fix: renumber (4c becomes 4d or moves under 4), one "last verified head" line, drop the
superseded status lines.

### A22. `.github/copilot-instructions.md` restates `AGENTS.md`

Severity: low. Effort: S. Parallel: yes.

Fifteen lines, of which twelve summarise the structure already in `AGENTS.md`. Two sources drift.

Fix: keep the first paragraph (pointer to `AGENTS.md` and the definition of done), delete the
rest.

### A23. The built-in look lives in the engine module

Severity: low. Effort: S (decide and document). Parallel: yes.

Module `wabar` (`src/host/qml/`) holds `Theme`/`ThemeSpec` (`bar_theme.h`, about 150 properties),
`Panel`, `PanelLayout`, `Tooltip`, `PopupWindow`, `WindowChrome`: the look and presets of the
built-in widgets. `AGENTS.md` says the engine holds "only what QML cannot do and generic
Quickshell compatibility"; a ported shell uses none of these. They are in `host` because
`theme.qml` loading is wired into `ShellHost`.

Fix: no move now. Add a paragraph to `docs/architecture.md` naming which `wabar` types are engine
(`ShellRoot` to `Region`) and which are the built-in look (`Theme` to `WindowChrome`), and that a
later step may move the latter to a `wabar.ui` module when `ui` gains C++. The styling proposal
already discusses this; link it.

### A24. `docs/architecture.md` rows are paragraphs

Severity: low. Effort: S. Parallel: yes (same agent as A2).

Several threading and singleton rows exceed 600 characters in one cell (for example the
`SystrayHook`, `AppsService` and `Thumbnails` rows). The tables are the parts contributors must
update, and they are the hardest to read.

Fix: one line per cell (owner, work, stop), and move the mechanism text to the feature's
`design.md`, which already exists for every feature.

## Checked and found in order

- Kernel admission: every file in `src/kernel/` has two or more users outside the kernel
  (`winrt_apartment` 3 features, `windows_build` 3 units, `shell_launch` 5, `app_activation` 3,
  `shell_icons` 2, `window_focus` 3, `live_desktop` and `app_data` many, `polled_source` 2 through
  `stat_source.h`).
- Dependency direction in includes: no `src/host` file includes `features/`; features include
  `host/` only from `tests/` and only `host/testing/`; `DEPENDS` edges match the architecture
  table; declaration order in `src/features/CMakeLists.txt` is topological.
- Conventions: no `m_` members; every feature has `design.md`; `win32/`, `qml/`, `testing/`,
  `tests/` are used as the rules say; QML-facing header basenames are unique.
- Feature sizes are reasonable (5 to 43 files). `brightness` has no widget and one consumer; it
  stays a library feature because it isolates a 778-line worker. `workspaces` holds two backends
  and stays one capability. No merge or split is needed beyond A11.
- CI (`.github/workflows/build.yml`) builds `x64-release`, `arm64-release`, `arm64-debug`.
- Allowlist: every path exists; every `GetProcAddress` site except the documented `EndTask` has
  a row.

## Work plan

Batches are independent of each other; items inside a batch share files and run in one agent in
the order given. Run batch 6 after batches 3 and 4 are merged.

| Batch | Items | Files | Effort |
|---|---|---|---|
| 1. Architecture tables | A2, A3, A4, A24 | `docs/architecture.md` only | S |
| 2. User docs and hygiene | A5, A21, A22, A18 | `docs/building.md`, `README.md`, `docs/plans/README.md`, `docs/styling.md`, `docs/shells.md`, `src/app/README.md`, `docs/plans/2026-10-30-handoff.md`, `.github/copilot-instructions.md`, `.gitignore`, worktrees | S |
| 3. Host naming and headers | A10, A15 | `src/host/qml/*`, `src/host/*taskbar*`, `src/host/quickshell/qml/quickshell_types.h`, `src/host/CMakeLists.txt` | M |
| 4. Features | A6, A11, A19, A13 | `src/features/taskbar/win32/`, `src/features/brightness/win32/`, `src/features/shell/`, `src/features/quickshell_io/`, `src/features/wallpapers/tests/`, `examples/gallery/` | M |
| 5. Build and test infrastructure | A7, A9, A17, A16 (document) | `tests/CMakeLists.txt`, `tools/check.ps1`, `cmake/wabar_feature.cmake`, `src/features/bluetooth/win32/`, `AGENTS.md`, `docs/building.md` (Notes) | M |
| 6. Test timing | A8 | the test files listed in A8, plus small accessors on `ShellHost`, `PolledSource` | M |
| 7. Risk record | A20, A23, A12 | `docs/release-checklist.md`, `docs/architecture.md` (one paragraph; after batch 1), `src/control/`, `src/host/qml/control_handler.*`, `src/features/wallpapers/qml/`, `src/app/wabarc/main.cpp` | M |
| 8. Quickshell unit | A1 | new `src/quickshell/`, `src/host/quickshell/` (removed), `src/app/wabar/quickshell_types_*.h` (moved), `src/modules/qml_modules.cmake`, `tools/check.ps1`, `AGENTS.md`, `docs/architecture.md` | L |

Batch 8 conflicts with 3 (headers it includes), 4 (`quickshell_io`) and 5 (`check.ps1`,
`AGENTS.md`); it runs last. Batches 1 to 7 are disjoint and can start together. A14 is not
scheduled; decide after batch 8 whether it is still wanted.
