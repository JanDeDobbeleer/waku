# Architecture

How the code is put together today and the rules that keep it there. The Widgets build (yasb-native)
was retired; its records are in `docs/archive/` and at the git tag `yasb-native-final`. The proposal
for the wabar layout is `docs/plans/2026-10-29-wabar-architecture-proposal.md`. Rules for agents are in
`AGENTS.md`.

## Layering

The old by-layer layout still holds until the restructure (migration step 4) replaces it:

```
spike/ (wabar)  ->  widgets/  ->  core/{bar,widgets} (ui)  ->  platform
```

Each layer is its own static library. The legacy libraries and every target under `src/` link
`wabar_options` (`cmake/wabar_options.cmake`: `/W4 /WX /permissive- /guard:cf /sdl ...`); the
former `spike/` targets do not yet (migration step 9):

| Target | Folder | Sources | Links PUBLIC | Links PRIVATE |
|---|---|---|---|---|
| `yasb_platform` | `platform/` | `platform/` | `Qt6::Core` | `pdh setupapi psapi ...` (Win32 libs), `Qt6::Network` |
| `yasb_ui` | `core/` | `core/bar/app_bar.*`, `core/widgets/backdrop.*` | `Qt6::Core`, `Qt6::Gui` | |
| `yasb_widgets` | `widgets/` | service and state code of the GitHub, systray, taskbar and wallpapers widgets | `yasb_ui`, `yasb_platform`, `nlohmann_json` | |
| `wabar_bar`, `wabar_control` | `spike/` | the wabar host (QML module `wabar`) and the control pipe | `yasb_ui`, Qt Quick | |
| `wabar_services`, `wabar_widgets` | `src/modules/qml_modules.cmake` | QML modules `wabar.services` and `wabar.widgets`, assembled from contributions (below) | `yasb_widgets`, the feature libraries, Qt Quick | |
| `wabar` (exe), `wabarc` (exe) | `spike/` | `spike/main.cpp`, `spike/cli/main.cpp` | | |
| `YASBNativeTrayHook[_arm64]` (DLL) | `src/features/tray/hook/` | `src/features/tray/hook/trayhook.cpp` | | static CRT, built next to `wabar.exe` |
| `wabar_tests` (exe) | `tests/` | `tests/main.cpp` and every `TESTS` contribution (`spike/tests/**`, `src/**/tests/`) | | `GTest`, `Qt6::Test` |

All targets share the repo root as include directory (includes are written `"core/..."`), so the
compiler alone does not reject a header-only include against the arrow. `tools\check.ps1` (also
with `-Fast` and in the pre-commit hook) has a `layering` step that fails on any `#include "..."`
from `platform/` into `core/`, `widgets/` or `spike/`; from `core/` into `widgets/` or `spike/`; and
from `widgets/` into `spike/`.

### The `src/` skeleton (migration in progress)

`docs/plans/2026-10-29-wabar-architecture-proposal.md` is being carried out; step 4 added the
skeleton, steps 5+ move code into it.

- `cmake/wabar_feature.cmake`: `wabar_feature(NAME <f> DEPENDS ... SOURCES ... WIN32_SOURCES ...
  QML_SOURCES ... QML_FILES ... TESTS ... SYSTEM_LIBS ...)` builds `wabar_<f>` (STATIC, or
  INTERFACE when QML only), links `wabar_kernel` and the `DEPENDS` libraries, checks the inner
  folder layout and that each `DEPENDS` feature is declared earlier, and appends its QML C++,
  `.qml` files and tests to global lists. `wabar_contribute()` is the same append for the legacy
  `spike/` folders. The decisions are in the comment at the top of the file.
- `src/CMakeLists.txt` adds `kernel`, `control`, `host`, `ui`, `features`, `app` (each if
  present). `src/kernel/` is the STATIC `wabar_kernel` (include root `src/`), see below.
- `src/modules/qml_modules.cmake` defines `wabar.services` and `wabar.widgets` from the global lists.
  It is included from the top-level `CMakeLists.txt`, not added as a subdirectory, because
  qmlcachegen names its outputs after the `.qml` path relative to the declaring directory and a
  `../` path breaks the build on Windows. Every `.qml` file gets `QT_RESOURCE_ALIAS <basename>`,
  so resource paths stay `qrc:/qt/qml/wabar/widgets/<Name>.qml`.
- `tests/CMakeLists.txt` defines `wabar_tests` from `tests/main.cpp` and the `TESTS` list.
- `tools\check.ps1` step `structure` checks rules S1-S8 (proposal, section 4.2), reading `DEPENDS`
  from the `wabar_feature(` calls: enforcing under `src/` and `tests/`, report-only for the legacy
  folders.

### The kernel (`src/kernel/`)

Helpers with no domain knowledge that two or more units use today and that change for reasons
outside any feature (admission criteria: proposal, section 6; a pull request that adds a kernel
file names its two users). Qt Core and Gui only; the Windows SDK only under `kernel/win32/`.
Namespace `wabar::kernel`, include `"kernel/<qt|win32>/<file>.h"`.

| File | What | Users |
|---|---|---|
| `win32/windows_build.*` | `windowsBuild()`, cached `RtlGetVersion` (allowlist row below) | host backdrop, `src/features/system_stats/win32/pdh.cpp`, `platform/power.cpp` |
| `win32/winrt_apartment.*` | `ensureWinrtApartment()` | bluetooth, brightness, connectivity, wlan |
| `win32/shell_launch.*` | `shellOpen`, `launchHiddenShell`, `startDetachedHidden`, `hideConsole` | Shell, ControlCenter, toast activation, komorebi, `Process` |
| `qt/live_desktop.*` | `liveDesktop([capability])`, `setCapabilityDisabled`, `capabilityDisabled` | the services' real constructors, `spike/main.cpp` (`--no-tray`) |
| `qt/polled_source.*` | `PolledSource`, the polled-sample base (QML face: `system_stats::StatSource`, `features/system_stats/qml/stat_source.h`) | `Memory`, `Cpu`, `Battery`, `Network` |
| `qt/app_data.*` | `appDataDir()`, `migrateLegacyAppData()` | taskbar pins, GitHub token, tray state, wallpaper thumbnails |

Rules the kernel owns:

- **One data folder**: every file wabar keeps lives in `%LOCALAPPDATA%\wabar` (`appDataDir()`,
  created on demand). Nothing writes to `%LOCALAPPDATA%\YASB` any more. The first `appDataDir()` call
  of a process copies `taskbar_pinned.json`, `github_token` and `systray_state_*.json` from `YASB`
  when `YASB` exists; it never moves or overwrites, logs one line when it copied something and
  leaves a marker file `migrated_from_yasb`, so the copy happens once (a file the user deletes from
  `wabar` does not come back). Wallpaper thumbnails are a cache and are rebuilt, not copied.
- **Live desktop**: a service that touches the real machine in its real constructor asks
  `kernel::liveDesktop()`; it is false on the offscreen platform (tests, screenshots). A command-line
  flag switches one capability off by name (`setCapabilityDisabled("tray", true)`); ask
  `liveDesktop("tray")`. No service tests `platformName()` itself.

## Threading

Everything that touches Qt Quick objects runs on the GUI thread. Worker threads never touch them; they
hand results back with queued signals or `QMetaObject::invokeMethod`.

| Thread | Owner | Work | Stopped by |
|---|---|---|---|
| GUI | `QGuiApplication` / the wabar host | QML engine, bars, services, hotkeys, tray | `wabar` quits and `exec()` returns |
| `QThreadPool` threads (github) | `platform::sendHttpAsync` (github mark-as-read, device-flow login, post-login fetch) | one blocking QtNetwork request each with its own `QNetworkAccessManager`; the result is posted back to the GUI thread and dropped if its context object is gone | finishes on its own (15 s transfer timeout); no join needed |
| `BrightnessControl` worker (`std::thread`) | `wabar.services.ControlCenter`, created on first use | owns DDC/CI reads and writes, display metadata (WinRT `DisplayMonitor`) and the coalesced slider writes; results return as queued signals; DDC probes fan out with `std::async` and are joined by the worker | `stop()` (called by `stopAll()`): sets the stop flag, wakes the queue, `join` |
| media session thread (`std::thread`, MTA) | `MediaSessionService`'s `SystemMediaSource` (`platform/media_session_winrt.cpp`), created on first use by `wabar.services.Media` | owns every GSMTC WinRT object; WinRT handlers only enqueue a task, the thread sleeps on a condition variable (no polling) and posts plain snapshots to the GUI thread; position interpolation is a GUI-thread `QTimer` active only while the current session plays | `stop()`: revokes handlers, wakes the queue, `join` |
| notifications worker (`std::thread`, MTA) and WNF callback | `NotificationService`'s `SystemNotificationSource` (`platform/notifications_system.cpp`), created on first use by `wabar.services.Notifications` | the worker owns the `UserNotificationListener` and runs list reads (100 ms debounce after each WNF change or menu open), remove, clear (all or one app) and toast activation tasks, sleeping on a condition variable (no polling); the WNF callback runs on a system thread, arms the debounced read and forwards a `u32` count to the sink, which posts both results to the GUI thread. Toast activation (`platform/toast_activation.cpp`) runs on this thread | `stop()`: unsubscribe WNF (waits for a running callback), wake the queue, `join` |
| `DndWatcher` WNF callback (system thread) | `DndWatcher` (`platform/dnd.cpp`), owned by `wabar.services.ControlCenter` | the callback reads `dndStatus()` and posts the result to the GUI thread (`statusChanged`) | `stop()`: unsubscribe WNF (waits for a running callback) |
| taskbar icon loader (`QThreadPool`, one thread) | `TaskbarIcons` (`src/features/taskbar/taskbar_icons.cpp`, owned by `wabar.services.TaskbarService`) | `WindowSystem::windowIcon` (WM_GETICON with a 200 ms timeout, shell item images, class icons), serial like Python's `IconService` thread; the image returns to the GUI thread with a queued call and is dropped if the loader is gone | destructor: `waitForDone` (a hung window costs at most the WM_GETICON timeout) |
| `WlanBackend` (`QThread`) | `wabar.services.Network` (created on the first refresh or connect) | wlanapi scan, connection query, profile edits, `Windows.Devices.WiFi` connect and disconnect; results return as queued signals | `~WlanBackend`: unregister notifications, `quit`, `wait` |
| wallpaper overlay decoder (`QThread`) | `WallpaperTransition` (`src/features/wallpapers/qml/wallpaper_transition.h`, one per transition) | decodes the new image and the transcoded copy of the old one; touches no Qt Quick object, posts the images back to the GUI thread through a checked `QPointer` | the thread deletes itself when it ends; a cancelled or destroyed transition never waits for it |
| `RealBluetoothBackend` (`QThread`) | `wabar.services.Bluetooth` (see singletons) | BluetoothAPIs and WinRT enumeration, battery reads, radio power, device watchers, audio connect/disconnect; results return as queued signals | destructor: cancel connect, `shutdown`, `quit`, `wait` |
| GlazeWM WebSocket thread (`std::thread`) | `GlazewmService` (`src/features/workspaces/glazewm_service.h`) via `glazewm::GlazewmSocket` (`src/features/workspaces/glazewm.h`, `win32/glazewm_socket.cpp`), created by the first widget subscription | owns the WinHTTP session, connect, blocking receive loop and the 4 s reconnect sleep; text frames and connection changes reach the GUI thread as queued signals; sends run on the GUI thread | `GlazewmSocket::stop()`: sets the stop event, closes the WinHTTP handles, `join` (idempotent, also from the destructor); the owner calls `GlazewmService::stop()` |
| komorebi pipe thread (`std::thread`) | `KomorebiService` (`src/features/workspaces/komorebi_service.h`) via `komorebi::KomorebiPipeListener` (`src/features/workspaces/komorebi.h`, `win32/komorebi_pipe.cpp`) | owns the `\\.\pipe\yasb-native-<uuid>` handle, runs `komorebic subscribe-pipe` and `state`, reads one JSON message per pipe message, reconnects; hands parsed state to the GUI thread with queued calls | `stop()`: sets the stop event (cancels waits and overlapped I/O), `join`, then `komorebic unsubscribe-pipe`; started lazily by the first subscription; the owner calls it |
| systray hook thread (`std::thread`) | `SystrayHook` (`src/features/tray/win32/tray_hook.h`), owned by `wabar.services.Systray`, started by the first Tray widget | creates nothing itself: the GUI thread creates the watchdog mutex and the `\\.\pipe\yasb_native_systray_monitor` pipe in the constructor; the worker injects `YASBNativeTrayHook[_arm64].dll` into Explorer (`SetWindowsHookEx`), owns the OVERLAPPED I/O, read buffer and completion event, parses each pipe message and emits queued signals to the GUI thread; retries after Explorer restarts (1 s no Explorer, 5 s failed injection, 3 s after the DLL disconnects) | `stop()` (also called by the destructor): sets the stop flag, releases the injection hook, cancels pending I/O, releases and closes the mutex (the DLL's watchdog then detaches and restores Shell_TrayWnd's window procedure within 5 s), `join` |

Platform data sources (`platform/`) are sampled off the GUI thread by the wabar stat sources; anything
they cache globally must be locked.

## Singletons and process-wide state

No new singleton without a row here and a reason.

| What | Where | Notes |
|---|---|---|
| media source-app name cache, shell-app cache | `platform/media_source_apps.cpp` | AUMID to display name; `QMutex`-guarded, written from the media session thread only |
| `wabar.services.Systray` | `src/features/tray/qml/systray.{h,cpp}` | the one tray host of a `wabar` run (the hook in Explorer), shared by every Tray widget of the bar; created by the QML engine on first use and destroyed with it, which stops the host. |
| `wabar.services.Network` | `spike/services/network_service.{h,cpp}` | the one Wi-Fi/Ethernet/VPN state of a `wabar` run, shared by every Wifi widget; created by the QML engine on first use and destroyed with it. Polls on the global `QThreadPool` (one sample at a time, applied on the GUI thread); the Wi-Fi scan/connect backend and its thread (`createWlanBackend`) start on the first refresh or connect and stop in the destructor. No source on the offscreen platform |
| cached Windows build number | `src/kernel/win32/windows_build.cpp` (`windowsBuild()`) | immutable |
| live-desktop disable flags | `src/kernel/qt/live_desktop.cpp` | set of capability names behind a `QMutex`; written once at startup (`--no-tray`), read by the services' real constructors |
| app data migration record | `src/kernel/qt/app_data.cpp` (`appDataDir`) | set of base folders already migrated in this process, behind a `QMutex`; the on-disk marker `migrated_from_yasb` makes the copy happen once per machine |
| `wabar.services.Bluetooth` | `spike/services/bluetooth_service.{h,cpp}` | the one Bluetooth backend of a `wabar` run (radio, paired devices, battery, connect/disconnect on the `RealBluetoothBackend` thread), shared by every Bluetooth widget; created by the QML engine on first use and owned by it, destroyed with it, which joins the backend thread. Never creates a backend when `kernel::liveDesktop("bluetooth")` is false or without an adapter (`platform::hasBluetoothAdapter`). |
| `wabar.services.Notifications` | `spike/services/notification_service.{h,cpp}` | the Action Center toasts for every Notifications widget of a `wabar` run, created by the QML engine as a singleton; wraps `platform::NotificationService` (WNF subscription and listener worker thread, stopped and joined when the engine destroys it); has no source on the offscreen platform |
| `wabar.services.TaskbarService` | `src/features/taskbar/qml/taskbar_service.{h,cpp}` | the one window tracker (`wabar::taskbar::WindowTracker`: shell hook and WinEvent hooks), pinned-apps store (`taskbar_pinned.json`) and window-icon cache of a `wabar` run, shared by every Taskbar widget; created by the QML engine on first use and destroyed with it (the tracker stops, the icon loader's one-thread pool is joined). Registers `image://windowicon`. The tracker starts when the first Taskbar widget hands over its bar window; it never starts on the offscreen platform. |
| `wabar.services.ControlCenter` | `spike/services/control_center_service.{h,cpp}` | the system side of every ControlCenter widget of a `wabar` run, created by the QML engine as a singleton: Do Not Disturb (its own `platform::DndWatcher`, WNF subscription stopped by its destructor), app theme, touch keyboard, power plans and modes, user name and picture, and the session and power actions behind an injectable launcher (shutdown, restart and sign out need a second request). Owns one `platform::BrightnessControl` (DDC/CI worker thread plus the power-setting window thread, joined by `stop()` in the destructor). Volume, microphone and the player are not repeated: the widget uses `AudioOutput`, `AudioInput` and `Media`. Inert on the offscreen platform |
| `wabar.services.GitHub` | `spike/services/github_service.{h,cpp}` | the GitHub notifications of a `wabar` run for every GitHub widget, created by the QML engine as a singleton on the GUI thread. Polls on a `QTimer`; each fetch is a one-shot task on the global `QThreadPool` (own network transport, ends with the request, nothing to join) and `platform::sendHttpAsync` carries mark-as-read and the device-flow requests; results return through a guard `QObject` owned by the client, so a response after the service is destroyed is dropped. Reads `YASB_GITHUB_TOKEN` or the token file the Python build uses, holds the token in memory only. No client on the offscreen platform |
| `wabar.services.Wallpapers` | `src/features/wallpapers/qml/wallpapers_service.{h,cpp}` | the wallpaper folder listing, current wallpaper, gallery state and toggle hotkey of a `wabar` run, shared by every Wallpapers widget; created by the QML engine on first use (`create()` registers `image://wallpaper`) and destroyed with it. Wraps the UI-free `WallpaperManager`, `collectImageFiles`, `ThumbnailCache` and `DesktopWallpaperBackend` (animated `engine` off). No thread of its own: the hotkey is `RegisterHotKey` (documented) on the GUI thread through a native event filter, unregistered in the destructor; thumbnails are decoded on Qt's image-reader thread by a provider that holds only the `ThumbnailCache`. Inert, no hotkey, when `kernel::liveDesktop("wallpapers")` is false. |

Function-local `static const` regexes and lookup tables are immutable and not listed.

## Error handling

- Exceptions never cross a Qt boundary: slots, lambdas connected to signals, event handlers,
  `paintEvent`, timers and any callback invoked by Qt. An exception escaping into Qt or
  `QApplication::exec()` is `std::terminate` with no log line.
- Every entry point from Qt into our code either uses non-throwing APIs or wraps its body in
  `try`/`catch` and logs with `qWarning`/`qCritical`.
- API boundaries return values, `std::optional` or a result struct. Internal exceptions thrown and
  caught within one module are fine (`backdrop.cpp`).

## Undocumented Windows API allowlist

Every undocumented API, hand-laid struct or numeric constant missing from the SDK is listed here
with the build it was verified on and its fallback. Add a row in the same commit that adds the call.

| API | Where | Verified on | Fallback |
|---|---|---|---|
| `user32!SetWindowCompositionAttribute` with `WCA_ACCENT_POLICY` (19), local `AccentPolicy` / `CompositionAttribData` structs | `core/widgets/backdrop.cpp` (`setAccentPolicy`) | Windows 11 build 26300, ARM64 | resolved with `GetProcAddress`; missing symbol or failure throws inside `enableBlur`, which catches and logs; bar keeps its plain background |
| `ntdll!NtQuerySystemInformation` class 80 (`SystemMemoryListInformation`), local `MemoryListInformation` struct | `src/features/system_stats/win32/memory.cpp` (`cachedBytes`) | Windows 11 build 26300, ARM64 | `GetPerformanceInfo` `SystemCache` when the symbol is missing or the call fails |
| `ntdll!RtlGetVersion` (documented for drivers; used because `GetVersionEx` lies to unmanifested apps) | `src/kernel/win32/windows_build.cpp` (`windowsBuild`; users: `core/widgets/backdrop.cpp`, `src/features/system_stats/win32/pdh.cpp` CPU counter choice, `platform/power.cpp` `isWindows10`) | Windows 11 build 26300, ARM64 | build 0: backdrop takes the pre-Windows 11 paths, PDH uses `% Processor Utility` and falls back to `% Processor Time` |
| Numeric DWM attributes: `DWMWA_USE_IMMERSIVE_DARK_MODE` (20), `DWMWA_WINDOW_CORNER_PREFERENCE` (33), `DWMWA_BORDER_COLOR` (34), `DWMWA_SYSTEMBACKDROP_TYPE` (38), undocumented Mica attribute 1029, colour sentinels `0xFFFFFFFF`/`0xFFFFFFFE` | `core/widgets/backdrop.cpp` | Windows 11 build 26300, ARM64 | gated on `windowsBuild()` (22000 / 22621); `DwmSetWindowAttribute` failures are caught and logged |
| `DEVPKEY_Bluetooth_Battery` ({104EA319-6EE2-4701-BD47-8DDBF425BBE5}, 2), a PnP property key read with `CM_Get_DevNode_PropertyW` | `platform/bluetooth.cpp` (`readBattery`) | Windows 11 build 26300, ARM64 (key and layout from Python YASB) | read failure or non-byte type gives no battery value; the menu then shows no battery |
| `KSPROPSETID_BtAudio` ({7FA06C40-B8F6-4C7E-8556-E8C33A12E54D}) one-shot ids 0 (reconnect) and 1 (disconnect) sent through `IKsControl::KsProperty` | `platform/bluetooth.cpp` (`setAudioConnection`) | key and ids from Python YASB; not exercised live on build 26300 | no KS filter or a refusing driver returns `no_ks_controls` / `oneshot_failed`; the menu shows "Failed to connect/disconnect device" |
| `ntdll!RtlSubscribeWnfStateChangeNotification` / `RtlUnsubscribeWnfStateChangeNotification` (resolved with `GetProcAddress`, local function typedefs) with WNF state names `WNF_SHEL_NOTIFICATION_TOTAL` (0x0D83063EA3B8D035, Windows 11 toast total) and `WNF_SHEL_NOTIFICATIONS` (0x0D83063EA3BC1035, Windows 10 unread badge); the buffer is read as a `u32` count | `platform/notifications_system.cpp` | Windows 11 build 26300, ARM64 (names, signatures and buffer layout from Python YASB) | a missing symbol or a non-zero status for both names logs a warning and leaves the count at 0 (icon `default`); clearing still works through the documented `UserNotificationListener` |
| Message `0x052C` (`WM_SPAWN_WORKER`) sent to `Progman` with `SendMessageTimeout`, to make Explorer create the WorkerW window behind the desktop icons; then `SetParent` of a Qt top-level window onto that WorkerW (dropping and restoring `WS_EX_LAYERED` around the call) | `src/features/wallpapers/win32/workerw.cpp` (`attachToWorkerW`) | Python YASB on Windows 11 build 26100; not exercised live on build 26300 in this port | no `Progman` or no WorkerW found: `attachToWorkerW` returns nullopt, the transition overlay is skipped (logged once), and the wallpaper is set without animation |
| Registry values `HKCU\Control Panel\Desktop`: `MaxVirtualDesktopDimension`, `MaxMonitorDimension` (DWORD), `PanoramaThreshold`, `PanoramaPortraitThreshold` (DWORD, thousandths), `TileWallpaper` / `WallpaperStyle` (strings), `HKCU\Control Panel\Colors\Background`, and the file `%APPDATA%\Microsoft\Windows\Themes\TranscodedWallpaper` | `src/features/wallpapers/win32/workerw.cpp` (`readWallpaperFit`, `readTranscodeLimits`, `readPanoramaThreshold`, `readDesktopBackgroundRgb`, `transcodedWallpaperPath`) | Python YASB (`engine.py`) on Windows 11 build 26100; not exercised live on build 26300 in this port | a missing or unreadable value uses Python's default (fit "fill", threshold 2.22 / 1.0, black background, no transcode resize); an unreadable transcoded copy skips the animation |
| `ntdll!NtQueryInformationProcess` class 60 (`ProcessCommandLineInformation`), read as a `UNICODE_STRING` | `src/features/taskbar/win32/app_identity_win.cpp` (`hostArguments`) | key and layout from Python YASB (`pin_context.py`) | symbol missing or call failing gives no host arguments; `mmc.exe` / `rundll32.exe` windows then share one `path:` identity |
| `PKEY_AppUserModel_ID` ({9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5), spelled out instead of taken from `propkey.h` | `src/features/taskbar/win32/app_identity_win.cpp` (`aumidForWindow`) | Windows 11 build 26300, ARM64 | property store unavailable falls back to the process AUMID, then to the exe path |
| `IPolicyConfig` ({f8679f50-850a-41cf-9c72-430f290290c8}) from `CLSID_PolicyConfigClient`, hand-laid vtable (`SetDefaultEndpoint` at slot 10 after `IUnknown`) | `platform/audio.cpp` (`setDefaultDevice`) | layout and ids from pycaw (same call Python YASB makes); not exercised live, so the default output is never changed by tests | `CoCreateInstance` or the call failing logs once and returns false; the source menu keeps the old default |
| `IQuietHoursSettings` ({6BFF4732-81EC-4FFB-AE67-B6C1BC29631F}) from `CLSID_QuietHoursSettings` ({F53321FA-34F8-4B7F-B9A3-361877CB94CF}), `get_UserSelectedProfile` / `put_UserSelectedProfile` directly after `IUnknown`, profile ids `Microsoft.QuietHoursProfile.*` | `platform/dnd.cpp` | Windows 11 build 26300, ARM64 (read verified live: `disabled`); the write path is not exercised live | creation or read failure reports `unknown`, which the Do Not Disturb tile shows as active (as Python does); a failed write logs and the tile state is unchanged |
| `ntdll!RtlSubscribeWnfStateChangeNotification` with WNF state name `WNF_SHEL_QUIETHOURS_ACTIVE_PROFILE_CHANGED` (0x0D83063EA3BF1C75), the callback ignores the buffer and re-reads the profile through `IQuietHoursSettings` | `platform/dnd.cpp` (`DndWatcher`) | name from Python YASB; not exercised live on 26300 | a missing symbol or a non-zero status logs a warning and the Do Not Disturb button then updates only on its own clicks |
| `ITipInvocation` ({37C994E7-432B-4834-A2F7-DCE1F13B834B}) from `CLSID_UIHostNoLaunch` ({4CE576FA-83DC-4F88-951C-9D0782B4E376}) `Toggle(HWND)` | `platform/touch_keyboard.cpp` | ids from Python YASB; not exercised live on build 26300 | server not reachable: start `TabTip.exe` with `ShellExecuteEx`, `WaitForInputIdle`, toggle again; a missing TabTip logs a warning |
| `powrprof!PowerGetUserConfiguredACPowerMode`, `PowerSetUserConfiguredACPowerMode`, `PowerSetUserConfiguredDCPowerMode` (power-mode overlays; absent on older Windows 10) and the overlay GUIDs 961cc777-2547-4f9d-8174-7d86181b8a7a / ded574b5-45a0-4f42-8737-46345c09c238 | `platform/power.cpp` (`activePowerMode`, `setPowerMode`) | Windows 11 build 26300, ARM64 (`PowerGet...` verified live: `Balanced`; the setters are not exercised live) | resolved with `GetProcAddress`; a missing getter falls back to the `ActiveOverlayAcPowerScheme` registry value, a missing setter returns false and the mode label keeps the chosen name; on Windows 10 the mode button is not built |
| `SHELLTRAYDATA` / 32-bit `NOTIFYICONDATA` layout (964 bytes: signature, message, then the 956-byte struct with 32-bit handles) that Explorer sends in `WM_COPYDATA` `dwData` 1 to `Shell_TrayWnd`; `dwData` 3 `WINNOTIFYICONIDENTIFIER` | `src/features/tray/tray_pipe.cpp` (`parseShellTrayData`), `src/features/tray/hook/trayhook.cpp` | layout from Python YASB 2.0.8, which runs on this build daily; the native parser is checked against an independently laid out struct in `src/features/tray/tests/test_systray_pipe.cpp`; not exercised against the live shell by this port yet (build 26300, pending the orchestrator's live run) | a short or malformed message is dropped with a warning; an unknown shell message type is ignored |
| Injecting a DLL into `explorer.exe` with `SetWindowsHookEx(WH_GETMESSAGE)` on the `Shell_TrayWnd` thread, which then subclasses `Shell_TrayWnd` (`SetWindowLongPtr(GWLP_WNDPROC)`) and writes tray messages to a named pipe | `src/features/tray/win32/tray_hook.cpp` (`ExplorerInjector`), `src/features/tray/hook/trayhook.cpp` | design and DLL source from Python YASB 2.0.8; not exercised live by this port yet (build 26300, ARM64 pending) | `createTrayHost` returns no host (the tray stays empty) when the DLL is missing, the architecture is unsupported, the process runs under emulation or the pipe and mutex cannot be created; a failed injection retries every 5 s; the DLL detaches itself within 5 s when the host's watchdog mutex is released or abandoned, restoring the saved window procedure |

Every undocumented API still to come gets a row here. `IDesktopWallpaper` itself (`src/features/wallpapers/win32/desktop_wallpaper_win.cpp`) is documented and needs no row.
