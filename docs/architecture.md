# Architecture

How yasb-native is put together today, where it is heading, and the rules that keep it there.
Sources: `DESIGN_REVIEW.md` sections 3 and 4, and the remediation plan
`docs/plans/2026-10-27-002-refactor-design-review-remediation-plan.md`. Rules for agents are in
`AGENTS.md`.

## Layering

Intended dependency direction:

```
app  ->  widgets  ->  core/{widgets,bar,util} (ui)  ->  core/{css,config}  ->  platform
```

Today all of `app/`, `core/`, `platform/` and `widgets/` compile into one static target,
`yasb_core`, so CMake does not enforce the arrows. Includes currently respect them except for one
build-level inversion: `core/widgets/CMakeLists.txt` compiles `widgets/stat_widgets.cpp` and the
cpu/memory/battery widgets into the core target. Plan unit R7 splits the targets:

| Target (planned) | Sources | Links PUBLIC | Links PRIVATE | Today |
|---|---|---|---|---|
| `yasb_platform` | `platform/` | `Qt6::Core` | `pdh setupapi psapi` | in `yasb_core` |
| `yasb_config` | `core/config/` + generated options | `nlohmann_json`, `Qt6::Core` | `yaml-cpp` | in `yasb_core` |
| `yasb_css` | `core/css/` | `Qt6::Widgets` | | in `yasb_core` |
| `yasb_ui` | `core/bar/`, `core/widgets/`, `core/util/` | `yasb_css`, `yasb_config`, `Qt6::Widgets` | | in `yasb_core` |
| `yasb_widgets` | `widgets/` | `yasb_ui`, `yasb_platform` | | in `yasb_core` (via `core/widgets/CMakeLists.txt`) |
| `yasb_app` | `app/` minus `main.cpp` | `yasb_widgets` | | in `yasb_core` (root `CMakeLists.txt`) |
| `yasb-native` | `app/main.cpp` | | `yasb_app` | links `yasb_core`, `windowsapp`, `runtimeobject` |

Until R7 lands, a new include that points against the arrow is a review failure even though it
builds.

## Startup and ownership

`app/main.cpp` is the composition root today, but much of the object graph lives in function
statics rather than in one owner (plan unit R6 introduces an explicit runtime object):

1. `winrt::init_apartment` (single-threaded), then `--version` short-circuits.
2. `setFontEngine()` sets `QT_QPA_PLATFORM=windows:fontengine=gdi` before `QApplication`, as Python
   YASB's `src/env.py` does.
3. `--config-home` sets `YASB_CONFIG_HOME`; `runBar` loads and validates `config.yaml`, preprocesses
   `styles.css` and installs the `TransitionEngine` on `qApp`.
4. `AppServices` (owned by a `unique_ptr` in `runBar`) owns the single-instance mutex, the file
   watcher, the hotkey service, the CLI server and weak `QPointer`s to the bar windows.
5. `registerStatWidgets()` fills the `WidgetRegistry`; `WidgetBuilder` creates widgets; the
   `BarWindow` takes ownership of them through Qt parenting.

## Threading

Everything that touches widgets runs on the GUI thread. Worker threads never touch `QWidget`s; they
hand results back with queued signals or `QMetaObject::invokeMethod` on `qApp`.

| Thread | Owner | Work | Stopped by |
|---|---|---|---|
| GUI | `QApplication` | widgets, bars, CSS engine, file watcher, tray | `exec()` returns |
| `PollingWorker` (`QThread`) | one per stat type (cpu, memory, battery), parented to `QCoreApplication` | runs the platform `sample()` every `update_interval`, emits `dataReady` to the GUI thread | `PollingWorker::stop()` / destructor at app exit |
| hotkey thread (`std::thread`) | `HotkeyService` | `RegisterHotKey` message loop; dispatches to the GUI thread | `stop()`: `PostThreadMessageW(WM_QUIT)` then `join` |
| CLI command threads (`std::thread`) | `CliServer` | `\\.\pipe\yasb_pipe_cli` accept/read/reply | `stop()`: sets `stopping_`, unblocks, `join` |
| CLI log thread (`std::thread`) | `CliServer` | `\\.\pipe\yasb_pipe_log` frames | `stop()`: `CancelSynchronousIo`, `join` |

Platform data sources (`platform/`) are called from `PollingWorker` threads; anything they cache
globally must be locked (`platform/memory.cpp` guards its PDH swap query with `g_swapMutex`).

## Singletons and process-wide state

No new singleton without a row here and a reason.

| What | Where | Notes |
|---|---|---|
| `EventBus::instance()` | `core/widgets/event_bus.h` | every `BaseWidget` connects in its constructor; tests share it across cases |
| `WidgetRegistry::instance()` | `core/widgets/registry.cpp` | `registerStatWidgets()` guards with a function-static bool (`widgets/stat_widgets.cpp`) |
| `GlobalState::instance()` | `core/bar/theme_class.cpp` | |
| `SchemaValidator::embedded()` | `core/config/schema_validator.cpp` | immutable after construction; fine |
| log file, observer, debug flag | `app/log.cpp` | process-global by nature; guarded by a `QMutex` |
| `tooltipOptions()` static | `core/widgets/tooltip.cpp` | also `CustomToolTip::active_` in `tooltip.h` |
| `processLauncher()`, `systemFunctions()` | `core/widgets/callbacks.cpp` | mutable from anywhere (test seam) |
| one `PollingWorker` per stat type | `widgets/{cpu,memory,battery}/*_widget.cpp` | first widget's `update_interval` wins for all later ones |
| PDH swap query | `platform/memory.cpp` (`g_swap*`) | mutex-guarded |
| `static TransitionEngine engine` | `app/main.cpp` (`runBar`) | outlives `QApplication`; plan unit R4 fixes the lifetime |
| cached Windows build number | `core/widgets/backdrop.cpp` (`windowsBuild()`) | immutable |

Function-local `static const` regexes and lookup tables are immutable and not listed.

## Error handling

- Exceptions never cross a Qt boundary: slots, lambdas connected to signals, event handlers,
  `paintEvent`, timers and any callback invoked by Qt. An exception escaping into Qt or
  `QApplication::exec()` is `std::terminate` with no log line.
- Every entry point from Qt into our code either uses non-throwing APIs or wraps its body in
  `try`/`catch` and logs with `qWarning`/`qCritical`.
- API boundaries return values, `std::optional` or a result struct (`LoadResult`, hotkey parsing,
  battery). Internal exceptions thrown and caught within one module are fine (`backdrop.cpp`,
  `label_format.cpp` `FormatError`, `callbacks.cpp`).
- Known violations to fix (plan unit R4): `nlohmann::json::at()`/`get<T>()` and the `const`
  `operator[]` on possibly missing keys in `app/main.cpp` and `core/config/loader.cpp`, `std::stoi`
  on format widths in `label_format.cpp`, `type_error` from `readField` in generated option headers.
  R4 also adds a top-level guard in `main` that logs and exits non-zero on an uncaught exception.

## Undocumented Windows API allowlist

Every undocumented API, hand-laid struct or numeric constant missing from the SDK is listed here
with the build it was verified on and its fallback. Add a row in the same commit that adds the call.

| API | Where | Verified on | Fallback |
|---|---|---|---|
| `user32!SetWindowCompositionAttribute` with `WCA_ACCENT_POLICY` (19), local `AccentPolicy` / `CompositionAttribData` structs | `core/widgets/backdrop.cpp` (`setAccentPolicy`) | Windows 11 build 26300, ARM64 | resolved with `GetProcAddress`; missing symbol or failure throws inside `enableBlur`, which catches and logs; bar keeps its plain background |
| `ntdll!NtQuerySystemInformation` class 80 (`SystemMemoryListInformation`), local `MemoryListInformation` struct | `platform/memory.cpp` (`cachedBytes`) | Windows 11 build 26300, ARM64 | `GetPerformanceInfo` `SystemCache` when the symbol is missing or the call fails |
| `ntdll!RtlGetVersion` (documented for drivers; used because `GetVersionEx` lies to unmanifested apps) | `core/widgets/backdrop.cpp` (`windowsBuild`), `platform/pdh.cpp` (CPU counter choice) | Windows 11 build 26300, ARM64 | build 0: backdrop takes the pre-Windows 11 paths, PDH uses `% Processor Utility` and falls back to `% Processor Time` |
| Numeric DWM attributes: `DWMWA_USE_IMMERSIVE_DARK_MODE` (20), `DWMWA_WINDOW_CORNER_PREFERENCE` (33), `DWMWA_BORDER_COLOR` (34), `DWMWA_SYSTEMBACKDROP_TYPE` (38), undocumented Mica attribute 1029, colour sentinels `0xFFFFFFFF`/`0xFFFFFFFE` | `core/widgets/backdrop.cpp` | Windows 11 build 26300, ARM64 | gated on `windowsBuild()` (22000 / 22621); `DwmSetWindowAttribute` failures are caught and logged |

The plan lists more coming with control_center, taskbar and wallpapers; each one gets a row.
