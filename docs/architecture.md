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
   registry. It populates its registry with `registerStatWidgets(registry)` `registerKomorebiWidgets(registry)` and `registerGlazewmWidgets(registry)`.
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
| `BrightnessControl` worker (`std::thread`) | shared by the control center through the registry's shared-service map (key `brightness`), created on first use | owns DDC/CI reads and writes, display metadata (WinRT `DisplayMonitor`) and the coalesced slider writes; results return as queued signals; DDC probes fan out with `std::async` and are joined by the worker | `stop()` (called by `stopAll()`): sets the stop flag, wakes the queue, `join` |
| media session thread (`std::thread`, MTA) | `MediaSessionService`'s `SystemMediaSource` (`platform/media_session_winrt.cpp`), created on first use by the media widget through the polling-worker registry's shared-service map | owns every GSMTC WinRT object; WinRT handlers only enqueue a task, the thread sleeps on a condition variable (no polling) and posts plain snapshots to the GUI thread; position interpolation is a GUI-thread `QTimer` active only while the current session plays | `stopAll()` calls the service's `stop()`: revokes handlers, wakes the queue, `join` |
| notifications worker (`std::thread`, MTA) and WNF callback | `NotificationService`'s `SystemNotificationSource` (`platform/notifications_system.cpp`), created on first use by the notifications widget through the polling-worker registry's shared-service map | the worker owns the `UserNotificationListener` and runs list reads (100 ms debounce after each WNF change or menu open), remove, clear (all or one app) and toast activation tasks, sleeping on a condition variable (no polling); the WNF callback runs on a system thread, arms the debounced read and forwards a `u32` count to the sink, which posts both results to the GUI thread. Toast activation (`platform/toast_activation.cpp`) runs on this thread; the shell icon and picture loaders of the menu run on a 2-thread `QThreadPool` owned by `NotificationsMenu` (`waitForDone` in its destructor) | `stopAll()` calls the service's `stop()`: unsubscribe WNF (waits for a running callback), wake the queue, `join` |
| `DndWatcher` WNF callback (system thread) | `DndWatcher` (`platform/dnd.cpp`), created on first use by the notifications widget through the shared-service map under key `dnd-watcher` | the callback reads `dndStatus()` and posts the result to the GUI thread (`statusChanged`) | `stopAll()` calls `stop()`: unsubscribe WNF (waits for a running callback) |
| taskbar Recycle Bin query (`QThreadPool::globalInstance()` task, one at a time) | `RecycleBinMonitor` (one per taskbar widget, `widgets/taskbar/recycle_bin_monitor.cpp`), started while a Recycle Bin button exists | `PinSupport::recycleBinInfo` (`SHQueryRecycleBin`) every 2 s; the answer returns to the GUI thread with a queued call and is dropped if the monitor stopped; "Empty Recycle Bin" runs `SHEmptyRecycleBin` (with its confirmation UI) as one more pool task | `stop()` / destructor: stops the timer and ignores an answer still on its way |
| taskbar icon loader (`QThreadPool`, one thread) | `TaskbarIcons` (one per taskbar widget, `widgets/taskbar/taskbar_icons.cpp`) | `WindowSystem::windowIcon` (WM_GETICON with a 200 ms timeout, shell item images, class icons), serial like Python's `IconService` thread; the image returns to the GUI thread with a queued call and is dropped if the loader is gone | destructor: `waitForDone` (a hung window costs at most the WM_GETICON timeout) |
| hotkey thread (`std::thread`) | `HotkeyService` | `RegisterHotKey` message loop; dispatches to the GUI thread | `stop()`: `PostThreadMessageW(WM_QUIT)` then `join` |
| CLI command threads (`std::thread`) | `CliServer` | `\\.\pipe\yasb_pipe_cli` accept/read/reply | `stop()`: sets `stopping_`, unblocks, `join` |
| CLI log thread (`std::thread`) | `CliServer` | `\\.\pipe\yasb_pipe_log` frames | `stop()`: `CancelSynchronousIo`, `join` |
| `WlanBackend` (`QThread`) | `WifiMenu` (created on first menu open) | wlanapi scan, connection query, profile edits, `Windows.Devices.WiFi` connect and disconnect; results return as queued signals | `~WlanBackend`: unregister notifications, `quit`, `wait` |
| `QThreadPool` threads (wallpapers) | one `WallpaperGallery` (private pool, two threads) | folder scan plus `IDesktopWallpaper::GetWallpaper` (COM per call), thumbnail decode and the PNG thumbnail cache; jobs only post queued calls back to the gallery | `~WallpaperGallery`: `clear`, `waitForDone` (before the window is destroyed) |
| wallpaper overlay decoder (`QThread`) | `WallpaperEngine` (one per transition) | decodes the new image and the transcoded copy of the old one; never touches widgets, posts the images back with a queued call | `~WallpaperEngine`: `wait`; the engine deletes itself when the overlay cannot be shown |
| `RealBluetoothBackend` (`QThread`) | shared by the Bluetooth widgets (see singletons) | BluetoothAPIs and WinRT enumeration, battery reads, radio power, device watchers, audio connect/disconnect; results return as queued signals | destructor: cancel connect, `shutdown`, `quit`, `wait` |
| GlazeWM WebSocket thread (`std::thread`) | `GlazewmService` (member of `Runtime`) via `platform::GlazewmSocket` (`platform/glazewm.h`), created by the first widget subscription | owns the WinHTTP session, connect, blocking receive loop and the 4 s reconnect sleep; text frames and connection changes reach the GUI thread as queued signals; sends run on the GUI thread | `GlazewmSocket::stop()`: sets the stop event, closes the WinHTTP handles, `join` (idempotent, also from the destructor); `Runtime::stop()` calls `GlazewmService::stop()` |
| komorebi pipe thread (`std::thread`) | `KomorebiService` (member of `Runtime`) via `platform::KomorebiPipeListener` | owns the `\\.\pipe\yasb-native-<uuid>` handle, runs `komorebic subscribe-pipe` and `state`, reads one JSON message per pipe message, reconnects; hands parsed state to the GUI thread with queued calls | `stop()`: sets the stop event (cancels waits and overlapped I/O), `join`, then `komorebic unsubscribe-pipe`; started lazily by the first widget subscription; `Runtime::stop()` calls it |
| systray hook thread (`std::thread`) | `SystrayHook` (`platform/systray_hook.h`), owned by `SystrayService` in the polling-worker registry's shared-service map (key `systray`), started by the first systray widget | creates nothing itself: the GUI thread creates the watchdog mutex and the `\\.\pipe\yasb_native_systray_monitor` pipe in the constructor; the worker injects `YASBNativeTrayHook[_arm64].dll` into Explorer (`SetWindowsHookEx`), owns the OVERLAPPED I/O, read buffer and completion event, parses each pipe message and emits queued signals to the GUI thread; retries after Explorer restarts (1 s no Explorer, 5 s failed injection, 3 s after the DLL disconnects) | `stop()` (called by `stopAll()` and the destructor): sets the stop flag, releases the injection hook, cancels pending I/O, releases and closes the mutex (the DLL's watchdog then detaches and restores Shell_TrayWnd's window procedure within 5 s), `join` |
| systray legacy thread (`std::thread`) | `SystrayLegacy` (`platform/systray_legacy.h`), fallback when the hook cannot be used (emulation, unsupported architecture, DLL missing) | creates and owns the hidden topmost `Shell_TrayWnd` window and its message loop; publishes tray icon messages as queued signals and forwards the messages to Explorer's real tray | `stop()`: posts `WM_CLOSE` to the window, `join` |

Platform data sources (`platform/`) are called from `PollingWorker` threads; anything they cache
globally must be locked.

## Singletons and process-wide state

No new singleton without a row here and a reason.

| What | Where | Notes |
|---|---|---|
| `Runtime` services | `app/runtime.h` | one app-owned instance each of the transition engine, widget registry, event bus, global UI state, polling-worker registry, app services, komorebi service (the single komorebi client; widgets reach it only through the `komorebi_subscribe`, `komorebi` and `komorebi_command` `EventBus` events) and GlazeWM service (the single GlazeWM client; widgets reach it only through the `glazewm_subscribe`, `glazewm` and `glazewm_command` `EventBus` events) |
| `SchemaValidator::embedded()` | `core/config/schema_validator.cpp` | immutable after construction; fine |
| log file, observer, debug flag | `app/log.cpp` | process-global by nature; guarded by a `QMutex` |
| `tooltipOptions()`, `CustomToolTip::active_` | `core/widgets/tooltip.{cpp,h}` | legacy process-wide tooltip settings and active-tooltip guard |
| `processLauncher()`, `systemFunctions()` | `core/widgets/callbacks.cpp` | mutable from anywhere (test seam) |
| `ClockSharedState::instance()` | `widgets/clock/clock_state.cpp` | alarms, snoozes and the countdown timer that every clock widget shows; one `alarms.json` per config folder, as in Python's `ClockWidgetSharedState`. Parented to `qApp`, GUI thread only, replaceable in tests via `resetInstance` |
| media source-app name cache, shell-app cache | `platform/media_source_apps.cpp` | AUMID to display name; `QMutex`-guarded, written from the media session thread only |
| shared services map | `core/widgets/polling_worker_registry.h` (`service<T>(key, make)`) | not process-global: owned by the runtime's registry; the media widget and the next-wave control center share one `MediaSessionService` under key `media`; the notifications widget and the control center share one `NotificationService` under key `notifications`; every taskbar widget shares one `PinStore` (the pinned apps file and its change signal, Python's `TaskbarSignalBus`) under key `taskbar_pins`; every wallpapers widget shares one `WallpaperManager` (random-change timer, "change running" guard, post-change commands, WorkerW overlay) under key `wallpapers`, as Python's `WallpaperManager` singleton |
| audio endpoints (`audio-output`, `audio-input`), brightness (`brightness`) | shared-service map in `core/widgets/polling_worker_registry.h`, created by `widgets/control_center/control_center_system.cpp` | one `platform::AudioEndpoints` per direction (Core Audio default endpoint, volume and device callbacks that re-post to the GUI thread) and one `platform::BrightnessControl`; the registry owns them and `stopAll()` releases the callbacks and joins the worker |
| `tidy.services.Systray` | `spike/services/systray.{h,cpp}` | the one tray host of a `tidybar` run (the hook in Explorer; no legacy mode), shared by every Tray widget of the bar; created by the QML engine on first use and destroyed with it, which stops the host. Replaces the Widgets build's `SystrayService` in tidy; the two never run together (the hook pipe and mutex admit one host) |
| `tidy.services.Network` | `spike/services/network_service.{h,cpp}` | the one Wi-Fi/Ethernet/VPN state of a `tidybar` run, shared by every Wifi widget; created by the QML engine on first use and destroyed with it. Polls on the global `QThreadPool` (one sample at a time, applied on the GUI thread); the Wi-Fi scan/connect backend and its thread (`createWlanBackend`) start on the first refresh or connect and stop in the destructor. No source on the offscreen platform |
| cached Windows build number | `core/widgets/backdrop.cpp` (`windowsBuild()`) | immutable |
| shared `WindowTracker` | `widgets/taskbar/taskbar_widget.cpp` (`registerTaskbarWidget`), `platform/window_tracker.{h,cpp}` | not process-global: a shared service under key `taskbar_windows` in the runtime's polling-worker registry, as Python's shared `TaskbarWindowManager`. One shell hook and one set of WinEvent hooks serve every taskbar widget; `stopAll()` calls its `stop()`. The Win32 WinEvent callback reaches it through one file-local pointer set while the hooks are installed |
| `tidy.services.Bluetooth` | `spike/services/bluetooth_service.{h,cpp}` | the one Bluetooth backend of a `tidybar` run (radio, paired devices, battery, connect/disconnect on the `RealBluetoothBackend` thread), shared by every Bluetooth widget; created by the QML engine on first use and owned by it, destroyed with it, which joins the backend thread. Never creates a backend on the offscreen platform, after `setDisabled(true)` or without an adapter (`platform::hasBluetoothAdapter`). Replaces the Widgets build's `acquireBackend` in tidy; the two never share a process |
| shared Bluetooth backend (`std::weak_ptr`) | `widgets/bluetooth/bluetooth_widget.cpp` (`acquireBackend`) | one enumeration thread and one set of WinRT watchers for all Bluetooth widgets, as Python's `BluetoothManager.acquire`; mutex-guarded first use, destroyed with the last widget |
| shared systray service | `widgets/systray/systray_service.h`, key `systray` in the registry's service map | one tray client for all bars, as Python's `SystrayWidget._systray_client_instance`: two bars must not both subclass `Shell_TrayWnd`; created by the first widget, the first widget also claims the "re-announce icons" request. Not created on the `offscreen` platform, so tests and `--screenshot` never touch the real tray |
| `tidy.services.Notifications` | `spike/services/notification_service.{h,cpp}` | the Action Center toasts for every Notifications widget of a `tidybar` run, created by the QML engine as a singleton; wraps `platform::NotificationService` (WNF subscription and listener worker thread, stopped and joined when the engine destroys it); has no source on the offscreen platform |

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
| `ntdll!RtlGetVersion` (documented for drivers; used because `GetVersionEx` lies to unmanifested apps) | `core/widgets/backdrop.cpp` (`windowsBuild`), `platform/pdh.cpp` (CPU counter choice), `platform/power.cpp` (`isWindows10`) | Windows 11 build 26300, ARM64 | build 0: backdrop takes the pre-Windows 11 paths, PDH uses `% Processor Utility` and falls back to `% Processor Time` |
| Numeric DWM attributes: `DWMWA_USE_IMMERSIVE_DARK_MODE` (20), `DWMWA_WINDOW_CORNER_PREFERENCE` (33), `DWMWA_BORDER_COLOR` (34), `DWMWA_SYSTEMBACKDROP_TYPE` (38), undocumented Mica attribute 1029, colour sentinels `0xFFFFFFFF`/`0xFFFFFFFE` | `core/widgets/backdrop.cpp` | Windows 11 build 26300, ARM64 | gated on `windowsBuild()` (22000 / 22621); `DwmSetWindowAttribute` failures are caught and logged |
| `DEVPKEY_Bluetooth_Battery` ({104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2), a PnP property key read with `CM_Get_DevNode_PropertyW` | `platform/bluetooth.cpp` (`readBattery`) | Windows 11 build 26300, ARM64 (key and layout from Python YASB) | read failure or non-byte type gives no battery value; the menu then shows no battery |
| `KSPROPSETID_BtAudio` ({7FA06C40-B8F6-4C7E-8556-E8C33A12E54D}) one-shot ids 0 (reconnect) and 1 (disconnect) sent through `IKsControl::KsProperty` | `platform/bluetooth.cpp` (`setAudioConnection`) | key and ids from Python YASB; not exercised live on build 26300 | no KS filter or a refusing driver returns `no_ks_controls` / `oneshot_failed`; the menu shows "Failed to connect/disconnect device" |
| `ntdll!RtlSubscribeWnfStateChangeNotification` / `RtlUnsubscribeWnfStateChangeNotification` (resolved with `GetProcAddress`, local function typedefs) with WNF state names `WNF_SHEL_NOTIFICATION_TOTAL` (0x0D83063EA3B8D035, Windows 11 toast total) and `WNF_SHEL_NOTIFICATIONS` (0x0D83063EA3BC1035, Windows 10 unread badge); the buffer is read as a `u32` count | `platform/notifications_system.cpp` | Windows 11 build 26300, ARM64 (names, signatures and buffer layout from Python YASB) | a missing symbol or a non-zero status for both names logs a warning and leaves the count at 0 (icon `default`); clearing still works through the documented `UserNotificationListener` |
| Message `0x052C` (`WM_SPAWN_WORKER`) sent to `Progman` with `SendMessageTimeout`, to make Explorer create the WorkerW window behind the desktop icons; then `SetParent` of a Qt top-level window onto that WorkerW (dropping and restoring `WS_EX_LAYERED` around the call) | `platform/workerw.cpp` (`attachToWorkerW`) | Python YASB on Windows 11 build 26100; not exercised live on build 26300 in this port | no `Progman` or no WorkerW found: `attachToWorkerW` returns nullopt, the transition overlay is skipped (logged once), and the wallpaper is set without animation |
| Registry values `HKCU\Control Panel\Desktop`: `MaxVirtualDesktopDimension`, `MaxMonitorDimension` (DWORD), `PanoramaThreshold`, `PanoramaPortraitThreshold` (DWORD, thousandths), `TileWallpaper` / `WallpaperStyle` (strings), `HKCU\Control Panel\Colors\Background`, and the file `%APPDATA%\Microsoft\Windows\Themes\TranscodedWallpaper` | `platform/workerw.cpp` (`readWallpaperFit`, `readTranscodeLimits`, `readPanoramaThreshold`, `readDesktopBackgroundRgb`, `transcodedWallpaperPath`) | Python YASB (`engine.py`) on Windows 11 build 26100; not exercised live on build 26300 in this port | a missing or unreadable value uses Python's default (fit "fill", threshold 2.22 / 1.0, black background, no transcode resize); an unreadable transcoded copy skips the animation |
| `dwmapi` ordinal 113 `DwmpActivateLivePreview(BOOL, HWND peek, HWND caller, UINT trigger = 3 (LPT_SUPERBAR), RECT*)`, resolved with `GetProcAddress(MAKEINTRESOURCEA(113))` | `platform/dwm_thumbnail.cpp` (`Win32LivePreview`) | key and signature from Python YASB (`bindings/dwmapi.py`); not exercised live on build 26300 | export missing or call failing: the taskbar logs once, peek stays off and the thumbnail preview works without it |
| `DWMWA_EXCLUDED_FROM_PEEK` (12), a numeric `DwmSetWindowAttribute` attribute | `platform/dwm_thumbnail.cpp` (`excludeFromPeek`) | Windows 11 build 26300, ARM64 | failure is ignored; the preview then fades with the desktop during peek |
| `ntdll!NtQueryInformationProcess` class 60 (`ProcessCommandLineInformation`), read as a `UNICODE_STRING` | `platform/app_identity.cpp` (`hostArguments`) | key and layout from Python YASB (`pin_context.py`) | symbol missing or call failing gives no host arguments; `mmc.exe` / `rundll32.exe` windows then share one `path:` identity |
| `PKEY_AppUserModel_ID` ({9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5), spelled out instead of taken from `propkey.h` | `platform/app_identity.cpp` (`aumidForWindow`) | Windows 11 build 26300, ARM64 | property store unavailable falls back to the process AUMID, then to the exe path |
| `IPolicyConfig` ({f8679f50-850a-41cf-9c72-430f290290c8}) from `CLSID_PolicyConfigClient`, hand-laid vtable (`SetDefaultEndpoint` at slot 10 after `IUnknown`) | `platform/audio.cpp` (`setDefaultDevice`) | layout and ids from pycaw (same call Python YASB makes); not exercised live, so the default output is never changed by tests | `CoCreateInstance` or the call failing logs once and returns false; the source menu keeps the old default |
| `IQuietHoursSettings` ({6BFF4732-81EC-4FFB-AE67-B6C1BC29631F}) from `CLSID_QuietHoursSettings` ({F53321FA-34F8-4B7F-B9A3-361877CB94CF}), `get_UserSelectedProfile` / `put_UserSelectedProfile` directly after `IUnknown`, profile ids `Microsoft.QuietHoursProfile.*` | `platform/dnd.cpp` | Windows 11 build 26300, ARM64 (read verified live: `disabled`); the write path is not exercised live | creation or read failure reports `unknown`, which the Do Not Disturb tile shows as active (as Python does); a failed write logs and the tile state is unchanged |
| `ntdll!RtlSubscribeWnfStateChangeNotification` with WNF state name `WNF_SHEL_QUIETHOURS_ACTIVE_PROFILE_CHANGED` (0x0D83063EA3BF1C75), the callback ignores the buffer and re-reads the profile through `IQuietHoursSettings` | `platform/dnd.cpp` (`DndWatcher`) | name from Python YASB; not exercised live on 26300 | a missing symbol or a non-zero status logs a warning and the Do Not Disturb button then updates only on its own clicks |
| `ITipInvocation` ({37C994E7-432B-4834-A2F7-DCE1F13B834B}) from `CLSID_UIHostNoLaunch` ({4CE576FA-83DC-4F88-951C-9D0782B4E376}) `Toggle(HWND)` | `platform/touch_keyboard.cpp` | ids from Python YASB; not exercised live on build 26300 | server not reachable: start `TabTip.exe` with `ShellExecuteEx`, `WaitForInputIdle`, toggle again; a missing TabTip logs a warning |
| `powrprof!PowerGetUserConfiguredACPowerMode`, `PowerSetUserConfiguredACPowerMode`, `PowerSetUserConfiguredDCPowerMode` (power-mode overlays; absent on older Windows 10) and the overlay GUIDs 961cc777-2547-4f9d-8174-7d86181b8a7a / ded574b5-45a0-4f42-8737-46345c09c238 | `platform/power.cpp` (`activePowerMode`, `setPowerMode`) | Windows 11 build 26300, ARM64 (`PowerGet...` verified live: `Balanced`; the setters are not exercised live) | resolved with `GetProcAddress`; a missing getter falls back to the `ActiveOverlayAcPowerScheme` registry value, a missing setter returns false and the mode label keeps the chosen name; on Windows 10 the mode button is not built |
| `SHELLTRAYDATA` / 32-bit `NOTIFYICONDATA` layout (964 bytes: signature, message, then the 956-byte struct with 32-bit handles) that Explorer sends in `WM_COPYDATA` `dwData` 1 to `Shell_TrayWnd`; `dwData` 3 `WINNOTIFYICONIDENTIFIER` | `platform/systray_pipe.cpp` (`parseShellTrayData`), `hook/trayhook.cpp`, `platform/systray_legacy.cpp` | layout from Python YASB 2.0.8, which runs on this build daily; the native parser is checked against an independently laid out struct in `tests/widgets/test_systray_pipe.cpp`; not exercised against the live shell by this port yet (build 26300, pending the orchestrator's live run) | a short or malformed message is dropped with a warning; an unknown shell message type is ignored |
| Injecting a DLL into `explorer.exe` with `SetWindowsHookEx(WH_GETMESSAGE)` on the `Shell_TrayWnd` thread, which then subclasses `Shell_TrayWnd` (`SetWindowLongPtr(GWLP_WNDPROC)`) and writes tray messages to a named pipe | `platform/systray_hook.cpp` (`ExplorerInjector`), `hook/trayhook.cpp` | design and DLL source from Python YASB 2.0.8; not exercised live by this port yet (build 26300, ARM64 pending) | `SystrayService` falls back to the legacy window when the DLL is missing, the architecture is unsupported, the process runs under emulation or the pipe and mutex cannot be created; a failed injection retries every 5 s; the DLL detaches itself within 5 s when the host's watchdog mutex is released or abandoned, restoring the saved window procedure |
| Replacing the shell tray: a hidden topmost window of class `Shell_TrayWnd` with the `TaskbandHWND` property, and forwarding of `WM_COPYDATA`, `WM_COMMAND`, `WM_ACTIVATEAPP` and messages above `WM_USER` (including `WM_USER + 372`, posted) to Explorer's real tray | `platform/systray_legacy.cpp` | behaviour from Python YASB's `SystrayMonitor`; not exercised live by this port yet (build 26300 pending) | only used when the hook is unavailable; if the window cannot be created the widget shows no icons |

Every undocumented API still to come gets a row here. `IDesktopWallpaper` itself (`platform/desktop_wallpaper.cpp`) is documented and needs no row.
