# workspaces: komorebi, GlazeWM and the shared Workspaces model

Current design of the capability. The widgets are `Workspaces` (with `WorkspaceDot`) and
`ActiveLayout`. The services are the `Workspaces` singleton, which hides which tiling window manager
runs, and the two clients it is built on, `Komorebi` and `GlazeWM`, which a widget may also use
directly.

## Layers

| Path | Content |
|---|---|
| `komorebi.h`, `win32/komorebi_pipe.cpp` | Process access (`KomorebiBackend`: is komorebi running, run `komorebic`), `launchKomorebic`, `monitorFromWindow`, and `KomorebiPipeListener`: the message-mode named pipe komorebi writes its state to. |
| `glazewm.h`, `win32/glazewm_socket.cpp` | `GlazewmSocket`: a WinHTTP WebSocket client with a receive thread and reconnect. |
| `komorebi_service.*`, `glazewm_service.*` | The protocol over an `EventBus`: subscribe, state events, commands. UI-free. |
| `komorebi_state.*`, `glazewm_state.*` | Pure functions over the parsed JSON (monitors, workspaces, windows, binding mode, tiling direction). |
| `event_bus.h` | `EventBus`: a signal hub between a client and its service. An internal detail; a later refactor removes it. |
| `command_line.*` | `quoteCmdLine`: Windows `list2cmdline` quoting for the `komorebic` command chain. |
| `qml/komorebi_client.*`, `qml/glazewm_client.*` | The `Komorebi` and `GlazeWM` QML singletons: a private `EventBus` plus the service, state exposed as properties. |
| `qml/workspaces_service.*` | The `Workspaces` singleton: one `monitors` model of either manager; `prefer` decides when both run. |
| `komorebi_workarea.*`, `win32/komorebi_workarea_win.cpp` | `KomorebiWorkAreaGuard`: tells komorebi that the work area changed (see "komorebi's work area"). |
| `testing/wm_fixtures.h` | Sample window-manager state and harnesses on the services' test seams. |
| `testing/fake_workarea.h` | A recording `WorkAreaNotifier` and a fake clock for the guard. |

## Threads

- komorebi pipe thread (`std::thread`, owned by `KomorebiPipeListener`): creates the pipe, runs
  `komorebic subscribe-pipe` and `state`, reads one JSON message at a time, reconnects. Results go
  to the GUI thread as queued calls. `stop()` sets the stop event (which also kills a running
  komorebic), joins and starts the unsubscribe detached.
- GlazeWM socket thread (`std::thread`, owned by `GlazewmSocket`): WinHTTP session, connect, blocking
  receive loop, 4 s reconnect sleep. Text frames and connection changes reach the GUI thread as queued
  signals; sends run on the GUI thread. `stop()` ends a blocked receive with
  `WinHttpWebSocketClose` and joins; only the worker closes the handles, since WinHTTP forbids
  closing a handle under a synchronous call on another thread.

Both rows are in the thread table of `docs/architecture.md`. Everything else runs on the GUI thread,
the work area guard included (no thread of its own).

## Behaviour

- Neither manager running is a normal state: `connected` stays false and `Workspaces.provider` is "".
- The komorebi monitor id is the `HMONITOR`; `Workspaces.monitors[].handle` matches
  `Window.window.monitorHandle`, so a bar shows the workspaces of its own screen.
- Commands (`focus`, layout toggles) are fire-and-forget hidden processes (`kernel::launchHiddenShell`
  for komorebic chains) so a slow `komorebic` never blocks the GUI thread.

## komorebi's work area

komorebi caches each monitor's work area and refreshes it only from three window messages to its
hidden window. A dock or scaling change bursts `WM_DEVICECHANGE`, `WM_DISPLAYCHANGE` and
`WM_SETTINGCHANGE` into its bounded channel (20 slots) while its consumer sleeps 3 s to verify a
monitor removal, so the `WM_SETTINGCHANGE` that waku's own app bar registration causes is dropped;
windows then tile under the bar. Analysis: `docs/plans/2026-10-31-workarea-sync.md`.

- **Guard.** `KomorebiWorkAreaGuard` (`komorebi_workarea.h`), created and owned by
  `src/app/waku/main.cpp` before the first reload and kept for the run, so a reload neither loses a
  pending notification nor depends on the `Komorebi` singleton.
- **Trigger.** Qt's screen signals: `QGuiApplication::screenAdded`, `screenRemoved`,
  `primaryScreenChanged` and, per screen, `QScreen::availableGeometryChanged`, `geometryChanged`,
  `logicalDotsPerInchChanged`. Qt emits `availableGeometryChanged` from the `WM_SETTINGCHANGE` that
  Explorer broadcasts once it has applied a work area, whoever changed it. No polling, no
  `komorebic state` read, no compare: komorebi compares its cache with `GetMonitorInfoW` itself and
  retiles only on a difference, so an unneeded notification is free.
- **Timing.** 500 ms after the last signal (a reload's remove and add coalesce), one notification at
  once and one 5 s later (after komorebi's 3 s verification). A new burst re-arms the pair. Idle, no
  timer runs. `nudge()` is the public entry for another trigger later (a komorebi pipe connect, for
  example); it sends at once and arms the late one. Neither `ShellHost::reloaded` nor the pipe
  connect triggers it today.
- **Message.** `FindWindowW(L"komorebi-hidden", nullptr)`, then
  `SendMessageTimeoutW(hwnd, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0, SMTO_ABORTIFHUNG, 250, ...)`.
  Windows refuses to queue WM_SETTINGCHANGE to another process (`SendNotifyMessageW` fails with
  `ERROR_MESSAGE_SYNC_ONLY`, 1159, seen live), so only a synchronous send works; the short timeout
  bounds a stuck komorebi. Behind `WorkAreaNotifier`; tests use `testing/fake_workarea.h`.
- **The class name** `komorebi-hidden` is a komorebi detail, not a Windows API: komorebi v0.1.41,
  `monitor_reconciliator/mod.rs`, `Hidden::create("komorebi-hidden")`. The guard matches the class
  alone (the title is not confirmed in the source). If komorebi renames or drops the window, nothing
  is found and nothing is sent.
- **Switch.** `--no-workarea-sync` disables the capability `workarea_sync`; the real notifier exists
  only when `kernel::liveDesktop("workarea_sync")`, so tests and the offscreen platform never send.
  `--safe` keeps the guard: it is about the desktop, not the config.
- **Logs.** `qInfo` per send (`komorebi work area nudge sent to 0x...`), `qDebug` once each time
  komorebi's window goes missing, `qWarning` once per run when the send is refused
  (`ERROR_ACCESS_DENIED`: komorebi runs elevated and waku does not).
- **Limits.** After a dock the `HMONITOR` of a display can change; komorebi's work area handler
  then finds no monitor and does nothing. Only komorebi's own display reconciliation fixes that; the
  late notification lands after it. GlazeWM re-reads the work area on every event and is not
  targeted.
- **Verified on.** Windows 10.0.26300, komorebi 0.1.41: to be filled in by the live check (plan
  section 6).

## Windows APIs

Documented only: named pipes, `CreateToolhelp32Snapshot` for the process check, `MonitorFromWindow`,
WinHTTP WebSocket, `FindWindowW` and `SendMessageTimeoutW` (`WM_SETTINGCHANGE`, `SPI_SETWORKAREA`).
`winhttp` is linked by this feature alone.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- GlazeWM WebSocket thread (`std::thread`) (`GlazewmService` (`src/features/workspaces/glazewm_service.h`) via `glazewm::GlazewmSocket` (`src/features/workspaces/glazewm.h`, `win32/glazewm_socket.cpp`), created by the first widget subscription): owns the WinHTTP session, connect, blocking receive loop and the 4 s reconnect sleep; text frames and connection changes reach the GUI thread as queued signals; sends run on the GUI thread; `GlazewmSocket::stop()`: sets the stop event, cancels a blocked receive with `WinHttpWebSocketClose` (250 ms close handshake), `join`, then the handles are closed (idempotent, also from the destructor); the owner calls `GlazewmService::stop()`

- komorebi pipe thread (`std::thread`) (`KomorebiService` (`src/features/workspaces/komorebi_service.h`) via `komorebi::KomorebiPipeListener` (`src/features/workspaces/komorebi.h`, `win32/komorebi_pipe.cpp`)): owns the `\\.\pipe\waku-<uuid>` handle, runs `komorebic subscribe-pipe` and `state`, reads one JSON message per pipe message, reconnects; hands parsed state to the GUI thread with queued calls; `stop()`: sets the stop event (cancels waits, overlapped I/O and a running komorebic, which `CreateProcessW` started with drained output pipes and which is killed), `join`, then starts `komorebic unsubscribe-pipe` detached; started lazily by the first subscription; the owner calls it
