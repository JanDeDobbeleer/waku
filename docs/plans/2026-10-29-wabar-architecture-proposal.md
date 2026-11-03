# wabar architecture proposal: vertical slices

Date: 2026-10-29. Branch `spike/qt-quick-bar`. Status: proposal for the owner's decision; nothing
here is implemented. Inputs: the include and link graph of the tree at `0ebd3ac`, `AGENTS.md`,
`docs/architecture.md`, `tools/check.ps1`, `spike/README.md`, the wabar design notes in
`docs/plans/`. Decision already taken: only wabar is kept; the Qt Widgets build is retired.

## 1. Purpose and principle

wabar's code is spread over six top-level folders that name frameworks, not purposes. The tray alone
lives in `platform/` (13 files), `widgets/systray/` (8), `spike/services/` (8), `spike/widgets/` (2),
`spike/tests/` (4), `tests/` (6) and `hook/` (1 DLL). Reading, changing, testing or deleting the tray
means touching seven places, and nothing stops the next change from adding an eighth.

The governing principle is **intent over framework**: a folder says what the code is for (audio,
tray, workspaces), and the framework (Win32, Qt Quick) is a subfolder inside it. Each capability is a
vertical slice that holds its backend interface, its Windows implementation, its fakes, its
QML-facing service, its QML widgets, its tests and its design note. A small, explicit shared kernel
holds what is truly cross-cutting. The rules are mechanical and `tools\check.ps1` enforces them.
Where the principle would produce a folder with one file, or split a real concept, this proposal
merges or extracts instead and says why.

## 2. What wabar depends on today, and what dies

Method: transitive closure of `#include "..."` from every file under `spike/`, plus companion `.cpp`
files of the included headers, plus link-only dependencies (factory functions declared in an
included header and defined elsewhere: `createCpuDataSource` and the others in `platform/pdh.cpp`,
`memory.cpp`, `battery.cpp`; `platform/media_source_apps.cpp` for `media_session.h`). Counts are
lines of `.h`/`.cpp`.

### 2.1 Link graph today

| Target | Links | Note |
|---|---|---|
| `wabar_services` (URI `wabar.services`) | `Qt6::Qml`, `Qt6::Quick`, `yasb_platform`, **`yasb_widgets`** | pulls the whole Qt Widgets widget library to reach 32 UI-free files |
| `wabar_bar` (URI `wabar`) | `Qt6::Quick`, `wabar_control`, **`yasb_ui`** | `yasb_ui` links `Qt6::Widgets`, `yasb_css`, `yasb_config` (yaml-cpp) for 3 files |
| `wabar_widgets` (URI `wabar.widgets`) | `wabar_bar`, `wabar_services` | QML only |
| `wabar_control` | `Qt6::Core`, `Qt6::Network` | clean |
| `wabar.exe`, `wabarc.exe`, `wabar_services_tests` | the above | none of the spike targets links `yasb_options`, so `/W4 /WX /guard:cf /sdl` are off for all wabar code |

Other findings from the graph and the check script:

- `tools\check.ps1` does not look at `spike/` at all: not in the format, layering, json-subscript or
  clang-tidy steps. A dry run of clang-format finds 5 unformatted files (`spike/bar/bar_host.cpp`,
  `spike/bar/bar_theme.h`, `spike/cli/main.cpp`, `spike/main.cpp`, `spike/tests/test_safe_mode.cpp`).
- The root `deploy` target ships `yasb-native.exe` only; wabar has no deploy target.
- `windowsBuild()` (`RtlGetVersion`) exists three times: `core/widgets/backdrop.cpp`,
  `platform/pdh.cpp`, `platform/power.cpp`.
- Eight services repeat `QGuiApplication::platformName() == "offscreen"` and five repeat a static
  `setDisabled(bool)`: one policy ("may this run touch the real desktop?") written eight times.
- `%LOCALAPPDATA%` is resolved in four places with two folder names: `YASB` (taskbar pins, GitHub
  token, wallpaper thumbnails) and `wabar` (tray pins).
- `launchHiddenShell` (a generic "run a command line hidden") lives in `platform/komorebi.h`; the
  `Shell` service includes the komorebi header to reach it.
- `widgets/github/github_format.h` needs the generated `core/config/options/github.h` (from
  `schema/schema.json`) only for `GithubIconsConfig`, a struct of icon strings.
- `widgets/wallpapers/wallpaper_engine.h` includes `<QWidget>`; it is the only QtWidgets use left in
  the closure. wabar constructs `WallpaperManager` with the animated engine off.
- `widgets/komorebi/komorebi_service.cpp` includes `core/widgets/callbacks.h` but calls only
  `platform::launchKomorebic` and `platform::launchHiddenShell`; the include looks stale.
- `spike/services/komorebi_client` and `glazewm_client` talk to the reused `KomorebiService` and
  `GlazewmService` through a private `EventBus`: the event protocol of the Widgets build survives as
  an internal adapter.

### 2.2 Kept: the wabar closure outside `spike/`

| Old folder | Files kept | Lines | Files |
|---|---|---|---|
| `spike/` | 129 (all) | 19,745 | everything, including 30 test files and the sample config |
| `platform/` | 76 of 84 | about 12,800 of 13,182 | all except the 8 dead files below |
| `widgets/` | 32 of 135 | 3,471 of 24,565 | `github/github_format.*`; `glazewm/glazewm_service.*`, `glazewm_state.*`; `komorebi/komorebi_service.*`, `komorebi_state.*`; `systray/icon_scale.*`, `systray_service.*`, `systray_state.*`, `tray_icon_registry.*`; `taskbar/pin_store.*`, `taskbar_icons.*`; `wallpapers/thumbnail_cache.*`, `wallpaper_engine.*`, `wallpaper_files.*`, `wallpaper_layout.*`, `wallpaper_manager.*` |
| `core/` | 9 of 74 | 697 of 9,926 | `bar/app_bar.*`, `widgets/backdrop.*`, `widgets/event_bus.h`, `widgets/callbacks.*` (stale include), `config/options/common.h`, `config/options/github.h` (generated) |
| `hook/` | 4 (all) | 559 | the tray hook DLL |
| `tools/` | `check.ps1`, `hooks/` | | rewritten, kept |

Optional deletions inside the kept set, each an owner decision (section 10):
`platform/systray_legacy.*` (227 lines; wabar has no legacy tray mode per `docs/architecture.md`) and
the animated wallpaper transition `widgets/wallpapers/wallpaper_engine.*`, `wallpaper_layout.*`,
`platform/workerw.*` (830 lines, the last QtWidgets use and two undocumented-API allowlist rows).

### 2.3 Dead once the Widgets build goes

| Path | Files | Size | Why dead |
|---|---|---|---|
| `app/` | 18 | 1,535 lines | Widgets entry point, runtime, CLI server, hotkeys, log, tray icon |
| `core/config/` (incl. `options/`, `generate_options.py`) | 30 | 3,521 lines | YAML loader, schema validator, generated option structs |
| `core/css/` | 7 | 2,575 lines | CSS preprocessor and transition engine |
| `core/bar/` except `app_bar.*` | 9 | about 1,150 lines | `BarWindow`, adaptive frame, screens, theme class |
| `core/widgets/` except the 4 kept files | 23 | about 2,300 lines | `BaseWidget`, registry, builder, polling workers, popup, tooltip, label format |
| `core/util/`, `core/version.*` | 5 | 37 lines | |
| `widgets/` except the 32 kept files | 103 | 21,094 lines | every `QWidget` widget, menus, fluent theme, clock, media, control center, ... |
| `platform/accent_color.*`, `app_volume.*`, `dwm_thumbnail.*`, `sound.*` | 8 | 396 lines | used only by Widgets classes |
| `tests/` (except salvage, 2.4) | 93 | 15,043 code lines | Widgets build test binary `yasb_tests` |
| `tests/vectors/` | 50 | 133,490 lines, 3.6 MB | parity vectors for YAML, CSS and Widgets labels |
| `tools/parity/` | 50 | 7,915 lines | vector export from Python YASB |
| `tools/ab/` | 6 | 657 lines | A/B against the Python bar |
| `tools/yasbc-native/` | 1 | 58 lines | Widgets build CLI |
| `schema/schema.json` | 1 | 16,508 lines, 412 KB | |
| `assets/sound/notification02.wav` | 1 | 387 KB | clock alarm of the Widgets build |
| `docs/parity-checklist.md`, `baseline.md`, `follow-ups.md`, `DESIGN_REVIEW.md`, plans `2026-10-27-001`, `-002` | 6 | | Widgets build records (delete or archive, section 10) |
| vcpkg `yaml-cpp`, Qt `Widgets` component | | | no wabar user (Widgets only if the wallpaper engine goes) |

In total about 44,000 of the 79,000 lines of C++ in the repository go, plus 150,000 lines of
generated data. `nlohmann-json` stays (komorebi and GlazeWM state, systray state, wallpaper files,
wabar tests) and `gtest` stays.

### 2.4 Tests to salvage before the deletion

Suites in `tests/` that cover kept code and have no wabar equivalent (about 140 tests):

| Source | Suites | Goes to |
|---|---|---|
| `tests/platform/test_glazewm_socket.cpp` | all (6) | `features/workspaces/tests/` |
| `tests/platform/test_systray_click.cpp` | all (16); check overlap with `spike/tests/test_systray.cpp` | `features/tray/tests/` |
| `tests/widgets/test_systray_pipe.cpp`, `test_tray_icon_registry.cpp`, `test_systray_icon_scale.cpp` | all (22, 26, 4) | `features/tray/tests/` |
| `tests/widgets/test_systray.cpp` | `SystrayStateStore`, `SystrayUpdateState` (6); `SystrayHostChoice` only if the legacy host stays | `features/tray/tests/` |
| `tests/widgets/test_brightness_policy.cpp` | all (3) | `features/brightness/tests/` |
| `tests/widgets/test_glazewm_state.cpp` | `GlazewmState`, `GlazewmService` (13) | `features/workspaces/tests/` |
| `tests/widgets/test_komorebi.cpp` | `KomorebiState`, `KomorebiServiceTest` (9) | `features/workspaces/tests/` |
| `tests/widgets/test_github.cpp` | `GithubApi`, `GithubFormat` (12) | `features/github/tests/` |
| `tests/widgets/test_taskbar.cpp` | `TaskbarIdentity`, `TaskbarPinIds`, `TaskbarPinStore`, `TaskbarIcons`, `TaskbarPinIcons`, `TrackerTest` (19) | `features/taskbar/tests/` |
| `tests/widgets/test_wallpapers.cpp` | the 27 tests are one `QWidget` fixture; inspect for manager and file-scan cases | `features/wallpapers/tests/` |

Parity vectors are not salvaged: every vector file tests YAML, CSS or a Widgets label.

## 3. Target tree

Include root is `src/` (`#include "features/tray/tray_host.h"`). Every feature has the same inner
layout; the table after the tree defines it once.

```
CMakeLists.txt                  project wabar; wabar_options; add_subdirectory(src), tests
CMakePresets.json               unchanged presets
vcpkg.json                      nlohmann-json, gtest
cmake/
  wabar_options.cmake           compile and link flags for every own target (was yasb_options)
  wabar_feature.cmake           wabar_feature(): one feature's library, QML contributions, tests
examples/
  config/                       the sample config home (bar.qml, theme.qml, widgets/); the default
                                config home of a development build
docs/
  architecture.md               slices, rules, threads, singletons, undocumented-API allowlist
  building.md                   build, deploy, check
  plans/                        dated plans and design history (unchanged)
tools/
  check.ps1                     format, structure, json-subscript, build, test, analyze
  hooks/                        pre-commit and commit-msg (unchanged)
tests/
  CMakeLists.txt                one test binary wabar_tests from every unit's tests/
  main.cpp                      gtest main with an offscreen QGuiApplication (was spike/tests/test_main.cpp)
  integration/                  tests that load built-in widgets of several features together
src/
  CMakeLists.txt                order: kernel, control, host, ui, features, modules, app
  kernel/                       the shared kernel: helpers with no domain knowledge, used by 2+ units
    win32/                      Windows helpers: build number, WinRT apartment, hidden launch, shell open
    qt/                         Qt helpers: live-desktop gate, polled source base, app data folder
    tests/
  control/                      the named-pipe protocol between a running bar and wabarc
    tests/
  host/                         the bar runtime: config model, bar windows, theme, popups, reload
    qml/                        QML module `wabar`: Config, Bar, BarLayout, Theme, PopupWindow, Tooltip, BarHost
    win32/                      app bar registration and DWM backdrop for bar and popup windows
    testing/                    bar fixtures for tests of the host and of widgets
    tests/
  ui/                           the shared look of widgets: Pill, Slider, DeviceMenu, DeviceRow (QML only)
  features/
    audio/                      output and input volume, mute, default device; the Volume widget
    bluetooth/                  radio, paired devices, battery, connect; the Bluetooth widget
    brightness/                 monitor brightness over DDC/CI and WMI, with its worker thread
    clock/                      date and time, calendar popup (QML only)
    control_center/             the quick-settings panel and the system toggles only it uses
    github/                     GitHub notifications, device-flow sign-in; the GitHub widget
    media/                      now playing (GSMTC), source app names; the Media widget
    network/                    Wi-Fi, Ethernet and VPN state, scan and connect; the Wifi widget
    notifications/              Action Center toasts, toast activation, Do Not Disturb
    shell/                      run programs and open targets from QML (Shell, Process)
    system_stats/               CPU, memory and battery samples; Cpu, Memory, Battery widgets
    taskbar/                    running and pinned apps: window tracking, app identity, pins, icons
    tray/                       the notification area: Explorer hook, icon state, pins; the Tray widget
      hook/                     the DLL injected into Explorer (no Qt, static CRT)
    wallpapers/                 wallpaper folder, set and cycle wallpapers, gallery, hotkey
    workspaces/                 komorebi and GlazeWM clients and the shared Workspaces model
  modules/                      CMake only: assembles wabar.widgets and wabar.services from contributions
  app/
    wabar/                      wabar.exe: composition root (main.cpp), SafeConfig.qml
    wabarc/                     wabarc.exe: the console client
```

Inner layout of every feature `src/features/<f>/` (folders a feature does not need are absent):

| Path | Intent | May contain |
|---|---|---|
| `CMakeLists.txt` | the feature's single declaration: sources, `DEPENDS`, system libraries, QML contributions, tests | one `wabar_feature(...)` call |
| `design.md` | the current design note of the capability (moved from `docs/plans/`) | prose |
| `*.h`, `*.cpp` (feature root) | the domain: backend interfaces, value types, UI-free logic, factory declarations | Qt Core and Qt Gui (`QImage`), no Windows headers, no QML |
| `win32/` | the Windows implementation of the root interfaces: Win32, COM, WinRT | Windows SDK and C++/WinRT headers |
| `qml/` | the QML-facing part: singletons, models, image providers (C++) and the feature's `.qml` widgets | `QtQml`, `QtQuick`, `QML_*` macros |
| `testing/` | fakes of the root interfaces, header-only | used by this feature's tests and by tests of dependent features |
| `tests/` | gtest sources | anything of the feature, kernel, `testing/` of `DEPENDS` |

Merges and extractions, with reasons:

- **network** = Wi-Fi + connectivity: one widget, one service (`Network`), one status object.
- **workspaces** = komorebi + GlazeWM + `Workspaces` + `ActiveLayout`: the shared `Workspaces` model is
  the concept; two slices would make the model reach into both. Inside: `komorebi_*` and `glazewm_*`
  file prefixes, `win32/komorebi_pipe.cpp`, `win32/glazewm_socket.cpp`.
- **system_stats** includes battery: same polled-sample pattern (`StatSource`), same popup
  (`StatPopup`, `StatGraph`). Power plans are a different concept and stay in control_center.
- **notifications** includes Do Not Disturb (`platform/dnd.*`): quiet hours is a notifications
  concept; control_center reads it through `DEPENDS notifications`.
- **brightness** is its own slice although only control_center uses it: 1,600 lines, its own worker
  thread, DDC/CI, a tested policy and a fake. A standalone Brightness widget would depend on it.
- **control_center** keeps power plans and modes, app theme, touch keyboard, user name and picture,
  and the session actions (`platform/power.*`, `theme.*`, `touch_keyboard.*`, `user_info.*`): each
  has one consumer and under 300 lines. Extract one when a second consumer appears.
- **taskbar** keeps window tracking, `WindowSystem`, app identity and pin support: today it is
  their only consumer. An `ActiveWindow` widget would trigger an extraction into
  `features/desktop_windows/` (not `windows/`, which reads like the OS subfolder).
- **clock** is QML only (`Clock.qml`, `Calendar.qml`). It stays a slice so that deleting the clock
  is deleting one folder.
- **ui** is not a feature and not kernel: it is the visual vocabulary that widgets of several
  features share (`Pill` in all, `Slider` in audio, media and control_center, `DeviceMenu`/`DeviceRow`
  in bluetooth and network). It is QML only and contributes to `wabar.widgets`.
- **control** is not inside host: `wabarc.exe` links it without Qt Quick.
- **Event bus**: `core/widgets/event_bus.h` moves into `features/workspaces/` as an internal detail;
  a later refactor removes the bus between `Komorebi`/`GlazeWM` and the reused services.

## 4. Dependency rules and their enforcement

### 4.1 Allowed edges

Edges are declared in each feature's `wabar_feature(... DEPENDS ...)`. A feature may use the root
headers (interfaces and value types) and the QML types of its `DEPENDS`; never their `win32/`,
`qml/` C++ headers or `testing/` outside tests. Initial graph (acyclic):

| Unit | May depend on |
|---|---|
| `kernel` | Qt Core, Qt Gui, Windows SDK (in `kernel/win32/` only) |
| `control` | `kernel`, Qt Core, Qt Network |
| `host` | `kernel`, `control`, Qt Quick |
| `ui` (QML) | `host` QML types (`Theme`, `Tooltip`, `PopupWindow`) |
| every feature | `kernel`; QML: `host` QML types, `ui` |
| `audio`, `bluetooth`, `network`, `github` | `shell` (QML only: "open settings" and "open URL" clicks use `Shell`) |
| `control_center` | `audio`, `media`, `notifications`, `brightness` |
| all other features | none |
| `app` | everything (composition root) |
| `tests/integration` | everything |

### 4.2 Rules

| Id | Rule | Checked by |
|---|---|---|
| S1 | Every `.h`/`.cpp`/`.qml` under `src/` belongs to exactly one unit (`kernel`, `control`, `host`, `ui`, `features/<f>`, `app/<exe>`); no sources in `src/` itself | `structure` step |
| S2 | Quoted includes: a unit includes its own files and `kernel/`. A feature also includes root headers (`features/<d>/*.h`, not subfolders) of each `<d>` in its `DEPENDS`. `host` includes `control/`. Only `app/` and `tests/integration/` include across freely | `structure` step; link errors for code (each feature library links only `wabar_kernel` and its `DEPENDS`) |
| S3 | Windows SDK and C++/WinRT angle includes (`windows.h`, `winrt/`, `shlobj.h`, `mmdeviceapi.h`, `wlanapi.h`, `winhttp.h`, `dwmapi.h`, `pdh.h`, `powrprof.h`, `setupapi.h`, `cfgmgr32.h`, `tlhelp32.h`, ... one list in `check.ps1`) only in `*/win32/**`, `features/tray/hook/` and `app/` | `structure` step |
| S4 | `QtQml`/`QtQuick` includes (`<QQml...>`, `<QQuick...>`, `<QJSEngine>`) and `QML_ELEMENT`, `QML_NAMED_ELEMENT`, `QML_SINGLETON`, `QML_UNCREATABLE`, `QML_ANONYMOUS` only in `*/qml/**` and `app/`. `<QWidget>`, `<QApplication>`, `QtWidgets` nowhere | `structure` step |
| S5 | Nothing outside `*/tests/**` and `tests/` includes `*/testing/**` | `structure` step |
| S6 | QML: a feature's `.qml` files reference services (`S.<Type>`) and widgets of their own feature or a `DEPENDS` feature only. The map type -> feature comes from the `QML_ELEMENT` class names and `QML_NAMED_ELEMENT` names in `features/*/qml/*.h` and the `.qml` file names | `structure` step |
| S7 | Header basenames are unique across all `qml/` folders (Qt's generated type registration includes them by name); `.qml` basenames are unique per QML module | `structure` step |
| S8 | `DEPENDS` graph is acyclic and names existing features | `structure` step (parses `wabar_feature(` calls) and CMake configure |

### 4.3 `tools\check.ps1`

| Step | Today | Proposed |
|---|---|---|
| format | `app core platform widgets tests tools/yasbc-native` | `src tests` |
| generated | `generate_options.py` drift | removed (no generated sources remain) |
| layering | 5 deny rules over `app core platform widgets` | replaced by `structure` (S1-S8), also with `-Fast` and `-Staged` |
| json-subscript | product dirs of the Widgets build | `src` |
| configure, build, test | unchanged | unchanged (`ctest` runs `wabar_tests`) |
| analyze | units under `app core platform widgets tools/yasbc-native` | units under `src/` |

The `structure` step reads the `DEPENDS` lists from `src/features/*/CMakeLists.txt` with a regex
over the `wabar_feature(` call, so it runs without a configured build tree and the CMake file stays
the single source of truth. Its rules are a table at the top of the step, like `$layerRules` today.
During the migration (section 9) it runs in report-only mode for paths not yet moved and in
enforcing mode for every moved unit.

## 5. Mapping old path -> new path

Paths after `src/` unless absolute. `win32/` files get a `_win` suffix where the root keeps a header
of the same name, so `audio_endpoints.h` (root) pairs with `win32/audio_endpoints_win.cpp`.

### 5.1 Directories

| Old | New |
|---|---|
| `spike/bar/` | `host/` (C++ logic), `host/qml/` (QML types and files), `host/win32/` |
| `spike/control/` | `control/` |
| `spike/main.cpp` | `app/wabar/main.cpp` |
| `spike/cli/main.cpp` | `app/wabarc/main.cpp` |
| `spike/config/` | `/examples/config/` |
| `spike/services/` | `features/<f>/qml/` (per feature, 5.2) |
| `spike/widgets/` | `features/<f>/qml/`, `ui/`, `app/wabar/SafeConfig.qml` |
| `spike/tests/` | `features/<f>/tests/`, `host/tests/`, `control/tests/`, `/tests/` |
| `spike/README.md` | `/README.md` (merged) |
| `hook/` | `features/tray/hook/` |
| `platform/` | `features/<f>/` root and `win32/`, `kernel/win32/` |
| `widgets/<x>/` (kept files) | `features/<f>/` root |
| `core/bar/app_bar.*` | `host/win32/app_bar.*` (header with `IAppBar` and `computeAppBarRect` at `host/`) |
| `core/widgets/backdrop.*` | `host/win32/backdrop.*`; `windowsBuild()` to `kernel/win32/windows_build.*` |
| `core/widgets/event_bus.h` | `features/workspaces/event_bus.h` |
| `core/widgets/callbacks.*`, `core/config/options/*` | deleted (stale include; `GithubIconsConfig` becomes `features/github/github_icons.h`) |
| `tools/check.ps1`, `tools/hooks/` | same paths |
| `docs/plans/2026-10-29-wabar-<f>-design.md`, `2026-10-28-wabar-systray-plan.md` | `features/<f>/design.md` (`git mv`; the dated plans stay as history in `git log`) |
| `docs/plans/2026-10-28-qt-quick-bar-config-design.md` | stays (cross-cutting: config, theme, popups, reload) |

### 5.2 Per capability

| Feature | From `platform/` | From `widgets/`, `core/` | From `spike/` | Tests and fakes |
|---|---|---|---|---|
| `kernel` | `winrt_apartment.*` -> `win32/winrt_apartment.*`; `shell_open.*` and `launchHiddenShell` (from `komorebi.*`) -> `win32/shell_launch.*`; the three `RtlGetVersion` copies -> `win32/windows_build.*` | `appDataDir` (from `widgets/systray/systray_state.*`) and the 3 other `LOCALAPPDATA` lookups -> `qt/app_data.*` | `services/stat_source.*` -> `qt/polled_source.*`; the offscreen checks and `setDisabled` flags -> `qt/live_desktop.*` | `kernel/tests/` new, small |
| `audio` | `audio.h` -> `audio_endpoints.h`; `audio.cpp` -> `win32/audio_endpoints_win.cpp` | | `services/audio_service.*`, `widgets/Volume.qml` -> `qml/` | `tests/test_audio.cpp`; `FakeEndpoints` -> `testing/fake_audio_endpoints.h` |
| `bluetooth` | `bluetooth.h` -> root; `bluetooth.cpp` -> `win32/bluetooth_win.cpp` | | `services/bluetooth_service.*`, `widgets/Bluetooth.qml` -> `qml/` | `test_bluetooth.cpp`; `FakeBackend` -> `testing/` |
| `brightness` | `brightness.h`, `brightness_policy.h` -> root; `brightness.cpp` -> `win32/brightness_win.cpp` | | | `tests/widgets/test_brightness_policy.cpp`; `FakeBrightness` (from `spike/tests/test_control_center.cpp`) -> `testing/` |
| `clock` | | | `widgets/Clock.qml`, `Calendar.qml` -> `qml/` | covered by `tests/integration/` |
| `control_center` | `power.*`, `theme.*` (-> `app_theme.*`), `touch_keyboard.*`, `user_info.*`: headers to root, `.cpp` to `win32/` | | `services/control_center_service.*`, `widgets/ControlCenter.qml` -> `qml/` | `test_control_center.cpp` |
| `github` | `github_api.*` -> root (Qt Network, no Windows headers) | `widgets/github/github_format.*` -> root; `GithubIconsConfig` -> `github_icons.h` | `services/github_service.*`, `widgets/GitHub.qml` -> `qml/` | `test_github.cpp` + salvaged `GithubApi`, `GithubFormat`; `ScriptTransport`, `FakeClient` -> `testing/` |
| `media` | `media_session.h`, `media_session.cpp` -> root; `media_session_winrt.cpp`, `media_source_apps.cpp` -> `win32/` | | `services/media_service.*`, `widgets/Media.qml` -> `qml/` | `test_media.cpp`; `FakeSource` -> `testing/` |
| `network` | `connectivity.*`, `wlan.*`: headers to root, `.cpp` to `win32/` | | `services/network_service.*`, `widgets/Wifi.qml` -> `qml/` | `test_network.cpp`; `FakeWifiBackend` -> `testing/` |
| `notifications` | `notifications.*`, `toast_xml.*` -> root; `notifications_system.cpp`, `toast_activation.cpp`, `dnd.cpp` -> `win32/`; `dnd.h`, `toast_activation.h` -> root | | `services/notification_service.*`, `widgets/Notifications.qml` -> `qml/` | `test_notifications.cpp`; `FakeSource` -> `testing/` |
| `shell` | (helpers come from kernel) | | `services/shell_service.*`, `services/child_process.*` -> `qml/` | `test_shell.cpp`, `test_process.cpp` |
| `system_stats` | `system_stats.h` -> root; `pdh.*`, `memory.*`, `battery.*` -> `win32/` | | `services/system_stats.*`, `widgets/Cpu.qml`, `Memory.qml`, `Battery.qml`, `StatGraph.qml`, `StatPopup.qml` -> `qml/` | `test_stat_sources.cpp`; `FakeCpu`, `FakeMemory`, `FakeBattery` -> `testing/` |
| `taskbar` | `window_system.h`, `window_tracker.*`, `pin_support.h`, `app_identity.h` -> root; `window_system_win32.cpp`, `window_tracker_hooks_win32.cpp`, `pin_support.cpp`, `app_identity.cpp` -> `win32/` | `widgets/taskbar/pin_store.*`, `taskbar_icons.*` -> root | `services/taskbar_service.*`, `taskbar_model.*`, `widgets/Taskbar.qml` -> `qml/` | `test_taskbar.cpp` + salvaged suites; `FakeSystem`, `FakePins` -> `testing/` |
| `tray` | `systray_types.h`, `systray_host.h`, `systray_pipe.*`, `systray_click.*` -> root (as `tray_*`); `systray_hook.*`, `systray_win.*` (`systray_legacy.*` if kept) -> `win32/` | `widgets/systray/systray_service.*`, `systray_state.*`, `tray_icon_registry.*`, `icon_scale.*` -> root | `services/systray.*`, `tray_icon_model.*`, `tray_image_provider.*`, `tray_pins.*`, `widgets/Tray.qml`, `TrayIconButton.qml` -> `qml/`; `/hook/` -> `hook/` | `test_systray.cpp`, `test_tray_model.cpp`, `test_tray_pins.cpp`, `test_tray_widget.cpp` + salvaged; the two copies of `FakeHost`/`FakeApi` -> one `testing/fake_tray_host.h` |
| `wallpapers` | `desktop_wallpaper.h` -> root, `.cpp` -> `win32/`; `window_focus.*` -> `win32/` (only user); `workerw.*` -> `win32/` only if the engine stays | `widgets/wallpapers/wallpaper_manager.*`, `wallpaper_files.*`, `thumbnail_cache.*` -> root; `wallpaper_engine.*`, `wallpaper_layout.*` per decision | `services/wallpapers_service.*`, `widgets/Wallpapers.qml`, `WallpaperGallery.qml` -> `qml/` | `test_wallpapers.cpp`; `FakeBackend`, `FakeHotkeys` -> `testing/` |
| `workspaces` | `komorebi.h` (minus `launchHiddenShell`), `glazewm.h` -> root; `komorebi.cpp` -> `win32/komorebi_pipe.cpp`; `glazewm.cpp` -> `win32/glazewm_socket.cpp` | `widgets/komorebi/komorebi_service.*`, `komorebi_state.*`, `widgets/glazewm/glazewm_service.*`, `glazewm_state.*`, `core/widgets/event_bus.h` -> root | `services/komorebi_client.*`, `glazewm_client.*`, `workspaces_service.*`, `widgets/Workspaces.qml`, `WorkspaceDot.qml`, `ActiveLayout.qml` -> `qml/` | `test_wm_clients.cpp`, `test_workspaces.cpp`, `test_active_layout.cpp` + salvaged; `wm_fixtures.h` -> `testing/` |
| `host` | | `core/bar/app_bar.*`, `core/widgets/backdrop.*` -> `win32/` | `bar/*.qml`, `bar_config.h`, `bar_theme.*`, `bar_host.*`, `control_handler.*` -> `qml/`; `popup_placement.*`, `screen_match.*` -> root (pure functions; their QObject wrapper in `qml/`); `bar_host_win32.cpp` -> `win32/` | `test_bar_host.cpp`, `test_theme.cpp`, `test_popups.cpp`, `test_screen_match.cpp`; `bar_fixtures.h` -> `testing/` |
| `control` | | | `control/control.*` | `test_control.cpp` |
| `ui` | | | `widgets/Pill.qml`, `Slider.qml`, `DeviceMenu.qml`, `DeviceRow.qml` | covered by `tests/integration/` |
| `app` | | | `main.cpp`, `cli/main.cpp`, `widgets/SafeConfig.qml` | `test_builtin_widgets.cpp`, `test_safe_mode.cpp` -> `/tests/integration/` |

## 6. The shared kernel

Admission criteria, all three required:

1. **Two or more units use it today** (not "might"). A helper with one user lives in that user.
2. **No domain knowledge**: it names no capability (no "tray", "komorebi", "wallpaper").
3. **Stable**: it changes for reasons outside any single feature (a Windows or Qt detail, a
   process-wide policy).

The kernel is `wabar_kernel` (static). Contents at the start:

| File | What | Users today |
|---|---|---|
| `kernel/win32/windows_build.*` | `windowsBuild()` via `RtlGetVersion` (allowlist row moves with it) | host (backdrop), system_stats (PDH counter choice), control_center (Windows 10 power modes) |
| `kernel/win32/winrt_apartment.*` | `ensureWinrtApartment()` | bluetooth, brightness, network (connectivity, wlan) |
| `kernel/win32/shell_launch.*` | `shellOpen(target, params)`, `launchHiddenShell(commandLine)` | shell, control_center, notifications (toast activation), workspaces (komorebic) |
| `kernel/qt/live_desktop.*` | one policy: may this run touch the real desktop (not offscreen, not disabled by a flag such as `--no-tray`); per-capability disable flags keyed by name | the 8 services that test `offscreen` today, `app/wabar/main.cpp` |
| `kernel/qt/polled_source.*` | `StatSource` renamed: interval, `active`, one sample at a time on the global `QThreadPool`, applied on the GUI thread | system_stats, network |
| `kernel/qt/app_data.*` | `dataDir()`: the one `%LOCALAPPDATA%\<product>` folder, created on demand | tray, taskbar, github, wallpapers |

Not in the kernel, with reasons:

- **Logging**: wabar logs with `qWarning`/`qCritical` and no log module exists. A kernel log
  module is added when a second need appears (log file, `wabarc log`); until then each feature
  may declare a `Q_LOGGING_CATEGORY` named `wabar.<feature>`.
- **Error policy**: a rule, not code. It stays in `AGENTS.md` and `docs/architecture.md`
  (exceptions never cross a Qt boundary).
- **QML registration helpers**: today every singleton writes `create(QQmlEngine*, QJSEngine*)` by
  hand, five of them because they register an image provider. The pattern is two lines; a helper
  would hide the image-provider registration. Revisit if a third pattern appears.
- **JSON**: `nlohmann-json` is used directly by workspaces, tray and wallpapers; no wrapper.
- **`ui/`**: QML look, part of the public `wabar.widgets` API, versioned with it; not a kernel.

Admission is reviewed in the structure check only indirectly (S2 lets everyone include the kernel),
so the criteria are a review rule in `AGENTS.md`: a pull request that adds a kernel file names its
two users.

## 7. CMake and QML module structure

### 7.1 Targets

| Target | Kind | Sources | Links PUBLIC | Links PRIVATE |
|---|---|---|---|---|
| `wabar_options` | INTERFACE | | | flags of `yasb_options` (`/W4 /WX /permissive- /guard:cf /sdl /utf-8 ...`) |
| `wabar_kernel` | STATIC | `src/kernel/**` minus tests | `Qt6::Core`, `Qt6::Gui` | `wabar_options`, `ntdll` lookups by `GetProcAddress`, `shell32`, `ole32`, `windowsapp` |
| `wabar_control` | STATIC | `src/control/` | `Qt6::Core`, `Qt6::Network` | `wabar_options` |
| `wabar_<f>` (15) | STATIC | feature root + `win32/` | `wabar_kernel`, `wabar_<d>` for each `DEPENDS` | `wabar_options`, the feature's own system libraries |
| `wabar_host` | STATIC + QML module `wabar` | `src/host/**` | `Qt6::Quick`, `wabar_control`, `wabar_kernel` | `wabar_options`, `dwmapi`, `shell32` |
| `wabar_services_qml` | STATIC + QML module `wabar.services` | every `features/*/qml/*.{h,cpp}` | `Qt6::Quick`, all `wabar_<f>` | `wabar_options`; private include dirs of every `features/*/qml/` |
| `wabar_widgets_qml` | STATIC + QML module `wabar.widgets` | every `features/*/qml/*.qml`, `src/ui/*.qml`, `src/app/wabar/SafeConfig.qml` | `Qt6::Quick` | `wabar_host`, `wabar_services_qml` |
| `wabar` | WIN32 exe | `src/app/wabar/main.cpp` | | the three module targets and their `...plugin` targets |
| `wabarc` | console exe | `src/app/wabarc/main.cpp` | | `wabar_control` |
| `WabarTrayHook` | SHARED | `src/features/tray/hook/` | | static CRT, no Qt, `AUTOMOC OFF` (as today) |
| `wabar_tests` | exe | every `*/tests/*.cpp`, `/tests/**` | | everything above, `GTest::gtest`, `Qt6::Test` |
| `deploy` | custom | | | `wabar.exe`, `wabarc.exe`, the hook DLL, `windeployqt --qmldir` over the module sources |

The system libraries move from the single `platform/CMakeLists.txt` list to the feature that calls
them, which documents what each capability touches. Tentative mapping, confirmed per move by
linking: `pdh` -> system_stats; `wlanapi` -> network; `bthprops`, `setupapi`, `cfgmgr32` ->
bluetooth; `dxva2` -> brightness; `powrprof` -> control_center (and system_stats if battery needs
it); `winhttp` -> workspaces (GlazeWM socket); `dwmapi` -> host; `propsys`, `shlwapi`, `psapi` ->
taskbar and media; `ws2_32`, `secur32`, `advapi32`, `version`, `winmm`, `uuid`, `oleaut32` by grep.

### 7.2 `wabar_feature()` and the module assembly

```cmake
# src/features/audio/CMakeLists.txt
wabar_feature(audio
  SOURCES      audio_endpoints.h win32/audio_endpoints_win.cpp
  SYSTEM_LIBS  ole32
  QML_TYPES    qml/audio_service.h qml/audio_service.cpp
  QML_WIDGETS  qml/Volume.qml
  TESTS        tests/test_audio.cpp
  DEPENDS      shell)
```

`wabar_feature` creates `wabar_audio`, links `wabar_kernel` and `wabar_shell`, and appends the
absolute paths of `QML_TYPES`, `QML_WIDGETS` and `TESTS` to global properties. `src/modules/`
is added after all features; it reads the properties, sets `QT_RESOURCE_ALIAS` to each `.qml`
file's basename (a source-file property is directory scoped, so it is set in the directory that
calls `qt_add_qml_module`), and defines `wabar.widgets` and `wabar.services`. The Qt documentation
of `qt_add_qml_module` describes `QT_RESOURCE_ALIAS` for exactly this case: a file whose source
location differs from its resource path. Deleting a feature is deleting its folder and its one
`add_subdirectory` line in `src/features/CMakeLists.txt`.

### 7.3 QML modules and URIs

The three user-facing URIs stay: `wabar`, `wabar.widgets`, `wabar.services`, version 1.0. Resource
paths stay (`qrc:/qt/qml/wabar/widgets/Clock.qml`) because the alias flattens each file into its
module's folder. No user config changes. C++ namespaces change (`wabar::services::Systray` becomes
`wabar::tray::Systray`), which QML does not see: a type's QML name is its class name or
`QML_NAMED_ELEMENT`, and rule S7 keeps those unique per module.

The widget/service name clash stays as designed (`Cpu`, `Memory`, `Battery`, `Media`, `Workspaces`
exist in both modules; widgets import `wabar.services as S`). It is also the main reason for
rejecting one QML module per feature (8).

### 7.4 Shipping and overriding built-in widgets

Built-in widgets stay compiled into `wabar.exe` (qmlcachegen plus resources, static plugins). A user
overrides or extends one by copying its file into the config home's `widgets/` folder, which the
config imports as a directory (`import "widgets"`), as `docs/plans/2026-10-28-qt-quick-bar-config-design.md`
describes. With the slices the source of a built-in is `src/features/<f>/qml/<Name>.qml`, one
predictable place. Whether `deploy` also ships the module's `.qml`, `qmldir` and `.qmltypes` files
as a read-only reference (for copying and for `qmllint`/`qmlls` in a config home) is question 7.

### 7.5 Alternatives rejected

| Option | For | Against | Verdict |
|---|---|---|---|
| A. Keep layers, rename (`platform` -> `os`, `services`, `widgets`) | smallest diff; three `git mv` | a capability stays spread over 4-7 folders; deleting the tray still touches all of them; the current problem is the spread, not the names | rejected |
| B. Slices; one static library per feature; QML aggregated into the 3 existing modules | CMake enforces C++ direction; URIs unchanged; 15 small libraries build in parallel; one place per capability | `qml/` C++ of all features compiles in one target, so only the script (S2, S6) keeps it apart; global properties add indirection to CMake | **chosen** |
| C. Slices; one QML module per feature, umbrella modules for the old URIs (`qmldir` `import` lines) | CMake isolates `qml/` code too; per-feature `qmllint` | the widget/service name clash forces two modules per feature (about 30 modules, 30 static plugin targets to link into the exe and the tests, each with type registrar, cache generation and resource steps); umbrella imports add a layer users can see in error messages | rejected; revisit if `wabar_services_qml` becomes a build bottleneck |
| D. Slices as folders, one library for all C++ | simplest CMake | only the script enforces direction; link errors no longer catch a reach | rejected: per-feature libraries cost little |
| E. Features at the repository root (`/audio`, `/tray`) | shorter paths | product code mixed with `docs`, `tools`, `cmake`; harder glob rules | rejected |
| F. Separate `windows/` libraries per feature (`wabar_audio_win32`) | link-level OS isolation | doubles the target count for a Windows-only product; the include rule S3 gives the same guarantee | rejected |

Build time: not measured. Expected effects: the deletion removes about 55 % of the C++ lines and
the QtWidgets, yaml-cpp and generated-header work; 15 feature libraries give Ninja more parallel
work than today's two large libraries; the module targets run the same type-registrar and
cache-generation steps as today.

## 8. Tests and fakes

- **Location**: each unit's tests live in its `tests/` folder; `tests/integration/` holds the two
  cross-feature suites (`test_builtin_widgets.cpp` loads every built-in widget; `test_safe_mode.cpp`
  builds `SafeConfig.qml`). `tests/main.cpp` is the gtest main with the offscreen
  `QGuiApplication` (today `spike/tests/test_main.cpp`).
- **One binary**, `wabar_tests`: one `windeployqt` post-build (slow step), one link, one
  `gtest_discover_tests` with `QT_QPA_PLATFORM=offscreen`. Per-feature binaries would repeat the
  deploy fifteen times. Selection by name instead: test suites are prefixed with the feature in
  CamelCase (`TrayModel...`, `WorkspacesKomorebi...`), so `--gtest_filter=Tray*` or
  `ctest -R Tray` runs one feature. The structure step checks the prefix.
- **Fakes**: today every fake is a class inside a test file, and some are duplicated (`FakeHost`
  and `FakeApi` in both `test_systray.cpp` and `test_tray_widget.cpp`; four different `FakeSource`
  classes). Each fake moves to the `testing/` folder of the feature whose interface it implements,
  header-only, one class per file (`features/tray/testing/fake_tray_host.h`). Tests of a dependent
  feature use them (`control_center` tests use `features/brightness/testing/fake_brightness.h`).
  Rule S5 keeps them out of product code.
- **Seams preserved**: every interface a fake implements today stays a root header of its feature
  (`AudioEndpoints`, `BluetoothBackend`, `WifiBackend`, `MediaSessionSource`, `NotificationSource`,
  `BrightnessControl`, `CpuDataSource`, `MemoryDataSource`, `BatteryDataSource`, `TrayHost`,
  `TrayWindowApi`, `WindowSystem`, `PinSupport`, `DesktopWallpaperBackend`, `HttpTransport`,
  `KomorebiBackend`, ...). Factories (`createSystemAudio`, `createBluetoothBackend`, ...) are declared
  at the root and defined in `win32/`, so the interface does not depend on the implementation.
- **Live desktop**: the rule stays: no test touches the real desktop. `kernel/qt/live_desktop`
  makes it one switch instead of eight checks.
- **Salvaged tests** (2.4) land in the feature folders in the same step as the deletion, so the
  kept code never runs untested.
- **Parity vectors**: gone with the Widgets build; Python YASB remains a behavioural reference for
  OS behaviour (tray messages, komorebi protocol), checked by unit tests with fixtures.

## 9. Migration plan

Choice: **delete the retired build first** (after salvaging its tests). Reasons: the wabar closure
outside `spike/` and `platform/` is 41 files, so carving it out is cheap; every later step then
reads and moves only living code (half the files); the `generated` check step, yaml-cpp, the
schema and QtWidgets leave the build, so every later green cycle is faster; and no code is moved
into a slice only to turn out dead. The cost, one large deletion commit, is mitigated by a tag and
by the commit being pure deletion plus CMake edits. The Widgets build stays readable at the tag.

Each step is one commit (or a short series) that ends green: `tools\check.ps1` exit 0 (format,
structure or layering, json-subscript, build `arm64-debug`, `ctest`).

| # | Step | Content | Size | Parallel |
|---|---|---|---|---|
| 0 | Tag | tag the tip before step 3; note it in `README.md` | XS | no |
| 1 | Salvage tests | copy the suites of 2.4 into `spike/tests/` (they compile against `yasb_widgets`/`yasb_platform` as they are) | M | no |
| 2 | Cut loose | replace `core/config/options/github.h` with a hand-written `widgets/github/github_icons.h`; drop the stale `callbacks.h` include; apply decisions 1 and 3 (wallpaper engine, systray legacy) | S | no |
| 3 | Delete the Widgets build | delete 2.3; shrink `yasb_ui` to `app_bar`, `backdrop`, `event_bus` and `yasb_widgets` to the 32 kept files; drop `yaml-cpp`, the `generated` step, `tests/CMakeLists.txt` `yasb_tests`, the old `deploy`; trim `AGENTS.md` to the rules that still apply | L (deletion) | no |
| 4 | Skeleton | `src/`, `cmake/wabar_options.cmake` (rename of `yasb_options`), `cmake/wabar_feature.cmake`, `src/modules/` defining `wabar.services` and `wabar.widgets` from global lists that `spike/` still fills, `/tests/` with the test binary; `check.ps1`: `structure` step (report-only outside moved units), format and json-subscript over `src spike`; format the 5 spike files (one commit, listed in `.git-blame-ignore-revs`) | M | no |
| 5 | Kernel | create `src/kernel/` (6 entries of section 6) and point the old code at it; delete the 3 `RtlGetVersion` copies | M | no |
| 6a-6n | Feature moves | one step per feature: `git mv` the files of 5.2, split headers and `win32/` sources where they share a file, update includes everywhere, move fakes to `testing/`, rename test suites, move the design note, enable `wabar_options` warnings for the feature library, update the threads and singletons rows in `docs/architecture.md` | S-M each | **yes**, see below |
| 7 | Host, control, ui | `spike/bar`, `spike/control`, `core/bar/app_bar`, `core/widgets/backdrop` -> `src/host`, `src/control`; `Pill`, `Slider`, `DeviceMenu`, `DeviceRow` -> `src/ui` | M | yes (with 6) |
| 8 | control_center | after audio, media, notifications, brightness | M | no |
| 9 | App and cleanup | `spike/main.cpp`, `cli/`, `SafeConfig.qml` -> `src/app`; `spike/config` -> `examples/config`; integration tests; delete the empty `spike/`, `platform/`, `widgets/`, `core/`, `hook/`; `structure` step fully enforcing; `wabar_options` on the module targets (fix the warnings `/W4 /WX` reveals in former `spike/` code) | M | no |
| 10 | Names | C++ namespaces `yasb::platform`, `yasb::widgets`, `wabar::services` -> `wabar::<feature>`; CMake project `wabar`; vcpkg name; hook DLL `WabarTrayHook[_arm64].dll` with pipe and mutex names together; one data folder (decision 2); `deploy` for wabar; CI workflow | M | partly (namespace per feature can ride along in 6) |
| 11 | Docs | rewrite `AGENTS.md` (structure rules, kernel admission, new commit scopes = unit names), `docs/architecture.md` (slices, threads, singletons, allowlist paths), `docs/building.md`, `README.md` | M | yes (with 10) |

Parallel work across agents (steps 6a-6n and 7), each in its own git worktree, after step 5:

| Wave | Units | Constraint |
|---|---|---|
| B1 | clock, shell, system_stats, audio, media, bluetooth, network, github, notifications, brightness, wallpapers, taskbar, tray, workspaces, host + control + ui | independent once the kernel exists: today no feature includes another feature's files except through the kernel helpers and DND |
| B2 | control_center | needs audio, media, notifications, brightness moved |

Shared files every move edits: `src/features/CMakeLists.txt` (one line), the legacy lists in
`spike/CMakeLists.txt`, `platform/CMakeLists.txt`, `widgets/CMakeLists.txt`, the test list, and
`docs/architecture.md`. These conflicts are line-local; land the moves through a merge train, each
rebased on the previous and re-checked. Fifteen moves fit three or four agents in parallel without
the train becoming the bottleneck. Each move's brief: the 5.2 row, the rules S1-S8, "no behaviour
change; rename only what the row says", and `tools\check.ps1` green.

## 10. Risks and open questions for the owner

1. **Animated wallpaper transition.** `WallpaperEngine` is a `QWidget` overlay on the WorkerW
   window (830 lines, two allowlist rows, the last QtWidgets dependency); wabar runs with it off.
   Proposal: delete it in step 2. Alternative: keep and port to a `QQuickWindow` later.
2. **Data folder.** Taskbar pins, the GitHub token and wallpaper thumbnails use
   `%LOCALAPPDATA%\YASB`; tray pins use `%LOCALAPPDATA%\wabar`. Proposal: everything in
   `%LOCALAPPDATA%\wabar`, with a one-time copy of `taskbar_pinned.json`, the token file and
   `systray_state_*.json` from `YASB` on first start; never write to `YASB` again.
3. **Systray legacy host.** Proposal: delete `platform/systray_legacy.*` (wabar never uses it per
   `docs/architecture.md`). Consequence: an x64 build under ARM64 emulation has no tray at all
   instead of a fallback.
4. **Hook DLL and pipe names.** `YASBNativeTrayHook[_arm64].dll`, `yasb_native_systray_monitor` and
   `YASBNativeTrayHookAlive` become `WabarTrayHook[_arm64].dll`, `wabar_systray_monitor` and
   `WabarTrayHookAlive` in step 10. Risk: a stale yasb-native process holding the old names is no
   longer detected as "another tray host"; both would then subclass `Shell_TrayWnd`. Acceptable only
   if yasb-native is no longer run on this machine.
5. **Repository and product rename.** Does the repository become `wabar` (GitHub rename, CI badge,
   `vcpkg.json` name, `yasb-native` working folder), and when: with step 10 or after the merge?
6. **Python YASB as reference.** `AGENTS.md` makes Python YASB the behavioural reference and requires
   porting every Python startup side effect. Proposal: keep it as a reference for OS behaviour only;
   drop the label-parity and startup-parity rules.
7. **Shipping widget sources.** Should `deploy` copy the built-in `.qml` files plus `qmldir` and
   `.qmltypes` into the distribution folder, so users can copy a widget and run `qmllint` on a config
   home without the repository?
8. **Old documents.** Delete or move to `docs/archive/`: `DESIGN_REVIEW.md`, `docs/parity-checklist.md`,
   `docs/baseline.md`, `docs/follow-ups.md`, the two 2026-10-27 port plans, `tools/parity/README.md`.

### Things the code did not tell me

- **Build time and warnings.** Nothing was built. How long a full build takes before and after, and
  how many `/W4 /WX` warnings the former `spike/` code produces once `wabar_options` applies, are
  unknown; step 9 is sized as M on a guess.
- **QML tooling with aliased files.** The Qt documentation covers `QT_RESOURCE_ALIAS` for resource
  paths; whether `qmllint`, `qmlls` and the build-directory module copy behave identically when the
  module's files come from 15 folders was not tried. Step 4 must prove it with two features before
  the parallel wave starts.
- **Override precedence.** That a type from `import "widgets"` reliably shadows a `wabar.widgets` type
  of the same name is stated by the config design note, not verified here.
- **Test overlap.** Whether the salvaged `test_systray_click.cpp` duplicates cases in
  `spike/tests/test_systray.cpp` and `test_tray_widget.cpp`, and whether any of the 27 wallpaper
  tests cover the manager without a widget, needs reading each case.
- **System library owners.** The mapping in 7.1 is tentative; `platform/CMakeLists.txt` links 26
  libraries to one target and the code does not say which file needs which.
- **WinRT apartment on the GUI thread.** The Widgets build called `winrt::init_apartment` in `main`;
  `spike/main.cpp` does not, and the features call `ensureWinrtApartment()` on their own threads.
  Whether any WinRT call happens on the GUI thread without an apartment was not traced.
- **Unused interface code.** Whether every function in the kept `platform/` headers still has a wabar
  caller (some served only Widgets classes, for example parts of `window_system.h` or
  `media_session.h`) was not checked function by function; the moves keep them, a later pass with
  a linker map can trim.

## 11. Decisions (owner, 2026-10-29)

Answers to the eight questions in section 10.

1. **Animated wallpaper transition: port it first.** Before the restructure, port `WallpaperEngine`
   from a `QWidget` overlay to a Qt Quick window, so no Qt Widgets code remains. Only then is the
   old engine deleted (step 2 no longer deletes it, it deletes the ported-away original).
2. **Data folder:** everything in `%LOCALAPPDATA%\wabar`; copy `taskbar_pinned.json`, the GitHub
   token file and `systray_state_*.json` from `YASB` once on first start; never write to `YASB`.
3. **Systray legacy host:** delete `platform/systray_legacy.*`.
4. **Hook DLL and pipe names:** rename to `WabarTrayHook[_arm64].dll`, `wabar_systray_monitor` and
   `WabarTrayHookAlive` in step 10. The old yasb-native build is not run any more.
5. **Repository rename:** after the merge to main, as a separate last step.
6. **Python YASB:** a reference for OS behaviour only; drop the label-parity and startup-parity rules
   from `AGENTS.md`.
7. **Widget sources:** `deploy` ships the built-in `.qml` files with `qmldir` and `.qmltypes`.
8. **Old documents:** move to `docs/archive/` with a note that they describe the retired Widgets build.
