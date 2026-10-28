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
compiler alone does not reject a header-only include against the arrow. `tools\check.ps1` (also
with `-Fast` and in the pre-commit hook) has a `layering` step that fails on any `#include "..."`
from `platform/` into `core/`, `widgets/` or `app/`; from `core/` into `widgets/` or `app/`; between
`core/css` and `core/config` or from either into ui; and from `widgets/` into `app/`.

## Startup and ownership

`app/main.cpp` creates an `app::Runtime` after `QApplication`; `Runtime` owns the application
services and the mutable UI services they share. Its local lifetime ends before `QApplication`:

1. `winrt::init_apartment` (single-threaded), then `--version` short-circuits.
2. `setFontEngine()` sets `QT_QPA_PLATFORM=windows:fontengine=gdi` before `QApplication`, as Python
   YASB's `src/env.py` does.
3. `--config-home` sets `YASB_CONFIG_HOME`; `runBar` loads and validates `config.yaml`, then uses
   the runtime-owned `TransitionEngine` to preprocess `styles.css` and install the event filter.
4. `Runtime` owns `AppServices`, `WidgetRegistry`, `EventBus`, `GlobalState`, and the polling-worker
   registry. It populates its registry with `registerStatWidgets(registry)` and `registerKomorebiWidgets(registry)`.
5. `WidgetBuilder` passes references to the event bus, global UI state and polling-worker registry
   in each `WidgetContext`; `BarDeps` receives the same `GlobalState`. `BarWindow` owns its widgets
   through Qt parenting.
6. After `exec()` returns, `Runtime::stop()` destroys app services and stops polling workers;
   `runBar` then destroys the bar and its widgets. The runtime and its `TransitionEngine` are
   destroyed before `QApplication`.

## Threading

Everything that touches widgets runs on the GUI thread. Worker threads never touch `QWidget`s; they
hand results back with queued signals or `QMetaObject::invokeMethod` on `qApp`.

| Thread | Owner | Work | Stopped by |
|---|---|---|---|
| GUI | `QApplication` / `Runtime` | widgets, bars, CSS engine, file watcher, tray | `Runtime::stop()` and then `exec()` returns |
| `PollingWorker` (`QThread`) | `Runtime`'s polling-worker registry, keyed by source and `update_interval` | runs the platform `sample()` at its configured interval, emits `dataReady` to the GUI thread | `Runtime::stop()` calls `stopAll()` before widgets are destroyed |
| `QThreadPool` threads (github) | `platform::sendHttpAsync` (github mark-as-read, device-flow login, post-login fetch) | one blocking QtNetwork request each with its own `QNetworkAccessManager`; the result is posted back to the GUI thread and dropped if its context object is gone | finishes on its own (15 s transfer timeout); no join needed |
| media session thread (`std::thread`, MTA) | `MediaSessionService`'s `SystemMediaSource` (`platform/media_session_winrt.cpp`), created on first use by the media widget through the polling-worker registry's shared-service map | owns every GSMTC WinRT object; WinRT handlers only enqueue a task, the thread sleeps on a condition variable (no polling) and posts plain snapshots to the GUI thread; position interpolation is a GUI-thread `QTimer` active only while the current session plays | `stopAll()` calls the service's `stop()`: revokes handlers, wakes the queue, `join` |
| hotkey thread (`std::thread`) | `HotkeyService` | `RegisterHotKey` message loop; dispatches to the GUI thread | `stop()`: `PostThreadMessageW(WM_QUIT)` then `join` |
| CLI command threads (`std::thread`) | `CliServer` | `\\.\pipe\yasb_pipe_cli` accept/read/reply | `stop()`: sets `stopping_`, unblocks, `join` |
| CLI log thread (`std::thread`) | `CliServer` | `\\.\pipe\yasb_pipe_log` frames | `stop()`: `CancelSynchronousIo`, `join` |
| `WlanBackend` (`QThread`) | `WifiMenu` (created on first menu open) | wlanapi scan, connection query, profile edits, `Windows.Devices.WiFi` connect and disconnect; results return as queued signals | `~WlanBackend`: unregister notifications, `quit`, `wait` |
| `RealBluetoothBackend` (`QThread`) | shared by the Bluetooth widgets (see singletons) | BluetoothAPIs and WinRT enumeration, battery reads, radio power, device watchers, audio connect/disconnect; results return as queued signals | destructor: cancel connect, `shutdown`, `quit`, `wait` |
| komorebi pipe thread (`std::thread`) | `KomorebiService` (member of `Runtime`) via `platform::KomorebiPipeListener` | owns the `\\.\pipe\yasb-native-<uuid>` handle, runs `komorebic subscribe-pipe` and `state`, reads one JSON message per pipe message, reconnects; hands parsed state to the GUI thread with queued calls | `stop()`: sets the stop event (cancels waits and overlapped I/O), `join`, then `komorebic unsubscribe-pipe`; started lazily by the first widget subscription; `Runtime::stop()` calls it |
| systray hook thread (`std::thread`) | `SystrayHook` (`platform/systray_hook.h`), owned by `SystrayService` in the polling-worker registry's shared-service map (key `systray`), started by the first systray widget | creates nothing itself: the GUI thread creates the watchdog mutex and the `\\.\pipe\yasb_native_systray_monitor` pipe in the constructor; the worker injects `YASBNativeTrayHook[_arm64].dll` into Explorer (`SetWindowsHookEx`), owns the OVERLAPPED I/O, read buffer and completion event, parses each pipe message and emits queued signals to the GUI thread; retries after Explorer restarts (1 s no Explorer, 5 s failed injection, 3 s after the DLL disconnects) | `stop()` (called by `stopAll()` and the destructor): sets the stop flag, releases the injection hook, cancels pending I/O, releases and closes the mutex (the DLL's watchdog then detaches and restores Shell_TrayWnd's window procedure within 5 s), `join` |
| systray legacy thread (`std::thread`) | `SystrayLegacy` (`platform/systray_legacy.h`), fallback when the hook cannot be used (emulation, unsupported architecture, DLL missing) | creates and owns the hidden topmost `Shell_TrayWnd` window and its message loop; publishes tray icon messages as queued signals and forwards the messages to Explorer's real tray | `stop()`: posts `WM_CLOSE` to the window, `join` |

Platform data sources (`platform/`) are called from `PollingWorker` threads; anything they cache
globally must be locked.

## Singletons and process-wide state

No new singleton without a row here and a reason.

| What | Where | Notes |
|---|---|---|
| `Runtime` services | `app/runtime.h` | one app-owned instance each of the transition engine, widget registry, event bus, global UI state, polling-worker registry, app services and komorebi service (the single komorebi client; widgets reach it only through the `komorebi_subscribe`, `komorebi` and `komorebi_command` `EventBus` events) |
| `SchemaValidator::embedded()` | `core/config/schema_validator.cpp` | immutable after construction; fine |
| log file, observer, debug flag | `app/log.cpp` | process-global by nature; guarded by a `QMutex` |
| `tooltipOptions()`, `CustomToolTip::active_` | `core/widgets/tooltip.{cpp,h}` | legacy process-wide tooltip settings and active-tooltip guard |
| `processLauncher()`, `systemFunctions()` | `core/widgets/callbacks.cpp` | mutable from anywhere (test seam) |
| `ClockSharedState::instance()` | `widgets/clock/clock_state.cpp` | alarms, snoozes and the countdown timer that every clock widget shows; one `alarms.json` per config folder, as in Python's `ClockWidgetSharedState`. Parented to `qApp`, GUI thread only, replaceable in tests via `resetInstance` |
| media source-app name cache, shell-app cache | `platform/media_source_apps.cpp` | AUMID to display name; `QMutex`-guarded, written from the media session thread only |
| shared services map | `core/widgets/polling_worker_registry.h` (`service<T>(key, make)`) | not process-global: owned by the runtime's registry; the media widget and the next-wave control center share one `MediaSessionService` under key `media` |
| cached Windows build number | `core/widgets/backdrop.cpp` (`windowsBuild()`) | immutable |
| shared Bluetooth backend (`std::weak_ptr`) | `widgets/bluetooth/bluetooth_widget.cpp` (`acquireBackend`) | one enumeration thread and one set of WinRT watchers for all Bluetooth widgets, as Python's `BluetoothManager.acquire`; mutex-guarded first use, destroyed with the last widget |
| shared systray service | `widgets/systray/systray_service.h`, key `systray` in the registry's service map | one tray client for all bars, as Python's `SystrayWidget._systray_client_instance`: two bars must not both subclass `Shell_TrayWnd`; created by the first widget, the first widget also claims the "re-announce icons" request. Not created on the `offscreen` platform, so tests and `--screenshot` never touch the real tray |

Function-local `static const` regexes and lookup tables are immutable and not listed. Stat widget
registration has no process-global guard; each runtime populates its own registry.

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
| `DEVPKEY_Bluetooth_Battery` ({104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2), a PnP property key read with `CM_Get_DevNode_PropertyW` | `platform/bluetooth.cpp` (`readBattery`) | Windows 11 build 26300, ARM64 (key and layout from Python YASB) | read failure or non-byte type gives no battery value; the menu then shows no battery |
| `KSPROPSETID_BtAudio` ({7FA06C40-B8F6-4C7E-8556-E8C33A12E54D}) one-shot ids 0 (reconnect) and 1 (disconnect) sent through `IKsControl::KsProperty` | `platform/bluetooth.cpp` (`setAudioConnection`) | key and ids from Python YASB; not exercised live on build 26300 | no KS filter or a refusing driver returns `no_ks_controls` / `oneshot_failed`; the menu shows "Failed to connect/disconnect device" |
| `SHELLTRAYDATA` / 32-bit `NOTIFYICONDATA` layout (964 bytes: signature, message, then the 956-byte struct with 32-bit handles) that Explorer sends in `WM_COPYDATA` `dwData` 1 to `Shell_TrayWnd`; `dwData` 3 `WINNOTIFYICONIDENTIFIER` | `platform/systray_pipe.cpp` (`parseShellTrayData`), `hook/trayhook.cpp`, `platform/systray_legacy.cpp` | layout from Python YASB 2.0.8, which runs on this build daily; the native parser is checked against an independently laid out struct in `tests/widgets/test_systray_pipe.cpp`; not exercised against the live shell by this port yet (build 26300, pending the orchestrator's live run) | a short or malformed message is dropped with a warning; an unknown shell message type is ignored |
| Injecting a DLL into `explorer.exe` with `SetWindowsHookEx(WH_GETMESSAGE)` on the `Shell_TrayWnd` thread, which then subclasses `Shell_TrayWnd` (`SetWindowLongPtr(GWLP_WNDPROC)`) and writes tray messages to a named pipe | `platform/systray_hook.cpp` (`ExplorerInjector`), `hook/trayhook.cpp` | design and DLL source from Python YASB 2.0.8; not exercised live by this port yet (build 26300, ARM64 pending) | `SystrayService` falls back to the legacy window when the DLL is missing, the architecture is unsupported, the process runs under emulation or the pipe and mutex cannot be created; a failed injection retries every 5 s; the DLL detaches itself within 5 s when the host's watchdog mutex is released or abandoned, restoring the saved window procedure |
| Replacing the shell tray: a hidden topmost window of class `Shell_TrayWnd` with the `TaskbandHWND` property, and forwarding of `WM_COPYDATA`, `WM_COMMAND`, `WM_ACTIVATEAPP` and messages above `WM_USER` (including `WM_USER + 372`, posted) to Explorer's real tray | `platform/systray_legacy.cpp` | behaviour from Python YASB's `SystrayMonitor`; not exercised live by this port yet (build 26300 pending) | only used when the hook is unavailable; if the window cannot be created the widget shows no icons |

The plan lists more coming with control_center, taskbar and wallpapers; each one gets a row.
