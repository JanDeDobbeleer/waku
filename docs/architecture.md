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

Each layer is its own static library (plan unit R7); the link lines encode the arrows, so a lower
target cannot link symbols from a higher one, and every target links `yasb_options`:

| Target | Folder (`CMakeLists.txt`) | Sources | Links PUBLIC | Links PRIVATE |
|---|---|---|---|---|
| `yasb_platform` | `platform/` | `platform/` | `Qt6::Core` | `pdh setupapi psapi` |
| `yasb_config` | `core/config/` | `core/config/` + generated options + embedded schema | `Qt6::Core`, `nlohmann_json` | `yaml-cpp` |
| `yasb_css` | `core/css/` | `core/css/` | `Qt6::Core` | `Qt6::Gui`, `Qt6::Widgets` |
| `yasb_ui` | `core/` | `core/bar/`, `core/widgets/`, `core/util/`, `core/version.cpp` | `yasb_css`, `yasb_config`, `Qt6::Widgets` | |
| `yasb_widgets` | `widgets/` | `widgets/` (incl. `registerStatWidgets`) | `yasb_ui`, `yasb_platform` | |
| `yasb_app` | `app/` | `app/` minus `main.cpp` | `yasb_widgets` | |
| `yasb-native` (exe) | `app/` | `app/main.cpp` | | `yasb_app`, `windowsapp`, `runtimeobject` |
| `yasbc-native` (exe) | root | `tools/yasbc-native/main.cpp` | | (none of ours) |
| `yasb_tests` (exe) | `tests/` | `tests/**` | | `yasb_app`, `GTest`, `Qt6::Test` |

Notes on the current graph:

- `yasb_ui` does not include `core/css` yet (the transition engine is installed by `main.cpp`); the
  link is kept because ui is the layer that applies CSS.
- Only `widgets/` uses `platform/` today; config and css do not.
- The WinRT libs sit on the executable because `winrt::init_apartment` in `main.cpp` is the only
  WinRT call. Move them to the library that first uses a WinRT projection.

All targets share the repo root as include directory (includes are written `"core/..."`), so the
compiler alone does not reject a header-only include against the arrow.

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
