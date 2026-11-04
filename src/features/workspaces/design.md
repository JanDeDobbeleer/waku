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
| `win32/shell_desktops_win.*` | `ShellDesktopApi`: the undocumented Task View interface (see "Shell interface"), and `RegistryChangeWatcher`. |
| `testing/wm_fixtures.h` | Sample window-manager state and harnesses on the services' test seams. |
| `testing/fake_workarea.h` | A recording `WorkAreaNotifier` and a fake clock for the guard. |

## Threads

- komorebi pipe thread (`std::thread`, owned by `KomorebiPipeListener`): creates the pipe, runs
  `komorebic subscribe-pipe` and `state`, reads one JSON message at a time, reconnects. Results go
  to the GUI thread as queued calls. `stop()` sets the stop event (which also kills a running
  komorebic), joins and starts the unsubscribe detached. While `komorebi.exe` does not run, the
  process check doubles its wait from 5 s to 30 s (`backoff.h`) and starts over once it runs.
- GlazeWM socket thread (`std::thread`, owned by `GlazewmSocket`): WinHTTP session, connect, blocking
  receive loop, reconnect sleep (4 s, doubling to 30 s while GlazeWM is not listening, back to 4 s
  after a connection). Text frames and connection changes reach the GUI thread as queued
  signals; sends run on the GUI thread. `stop()` ends a blocked receive with
  `WinHttpWebSocketClose` and joins; only the worker closes the handles, since WinHTTP forbids
  closing a handle under a synchronous call on another thread.

Both rows are in the thread table of `docs/architecture.md`. Everything else runs on the GUI thread,
the work area guard included (no thread of its own).

## Behaviour

- Neither manager running is a normal state. `Workspaces.provider` is then "windows" (the virtual
  desktops, below) and only "" when Windows exposes no desktop list either.
- The komorebi monitor id is the `HMONITOR`; `Workspaces.monitors[].handle` matches
  `Window.window.monitorHandle`, so a bar shows the workspaces of its own screen.
- Commands (`focus`, layout toggles) are fire-and-forget hidden processes (`kernel::launchHiddenShell`
  for komorebic chains) so a slow `komorebic` never blocks the GUI thread.

## Windows virtual desktops

`VirtualDesktops` (`qml/virtual_desktops_service.*`) is the provider for a bar without a tiling
manager; `Workspaces` maps it to `provider: "windows"`.

- **Read.** Explorer keeps the desktop list, the shown desktop and the names in the registry
  (`win32/virtual_desktops_win.cpp`, allowlist row in `docs/architecture.md`). A 500 ms `QTimer`
  polls it every 5 s as a safety net (500 ms where no change hook is active); changes arrive
  sooner from the hook (see "Shell interface"). The windows per desktop come from the documented
  `IVirtualDesktopManager::GetWindowDesktopId` over top-level, titled, unowned windows. Each
  desktop carries `windows`, the HWNDs ascending (EnumWindows order follows focus, so an unsorted
  list would fire `changed()` on every focus change), and `windowCount` (its size). Windows of
  waku's own process are left out: the jump anchor is visible for 700 ms. The filter is
  `countsAsWindow`, not the taskbar's; the shell joins the HWNDs with the taskbar rows
  (`hwnd` role), so the taskbar's rules decide what is listed and in which order. Minimized
  windows stay on their desktop. A pinned ("show on all desktops") window that reports no listed
  desktop (GUID_NULL, an error or an unknown id) is put on the shown desktop when
  `IsWindowOnCurrentVirtualDesktop` says so; which of the three Windows returns for a pinned window
  is not verified live yet, the fallback covers all of them. `refresh()` is invokable so a shell can
  look sooner than the poll.
- **Switch.** One jump to any desktop. With the shell interface open the call is
  `SwitchDesktopWithAnimation`, the one Win+Ctrl+Arrow makes (animated; `VirtualDesktops.animate`
  false uses the unanimated `SwitchDesktop`). Without it, or when it fails, a small off-screen
  window of waku's own is moved to the target desktop with the documented
  `IVirtualDesktopManager::MoveWindowToDesktop` (while it is
  hidden; the desktop service only reports a window it has seen shown) and activated;
  Windows then switches to the desktop of the active window. The window must be a plain popup: the
  desktop service does not track tool or layered windows (`GetWindowDesktopId` fails and the move
  does nothing). It hides itself 700 ms later, after the animation. `SetForegroundWindow` needs the
  foreground right, which a click on a no-activate bar does not give; `SwitchToThisWindow` (the call
  Alt+Tab uses, documented but marked not for general use) has no such restriction and is the
  fallback. A failed jump logs `virtual desktops: jump failed: <reason>` and does nothing.
  Keystrokes (Win+Ctrl+Left/Right) were tried first and dropped: Windows ignores a key pressed while
  the last switch animates, so a switch of several desktops moved one.
- **Monitors.** Desktops are not per monitor, so the model has one entry with handle 0 that every
  bar shows (`Workspaces.qml` accepts handle 0).
- **Priority.** A connected komorebi or GlazeWM wins; `prefer = "windows"` forces the desktops.
- **Create.** `switchTo` with the index just past the last desktop (below 20) creates the next
  one and shows it. With the shell interface, `CreateDesktop` makes it without showing it and the
  service then jumps to it; without, one Win+Ctrl+D (the only call Windows documents) does both. A
  shell draws one spare dot for it (Caelestia).
- **Close.** `remove(index)` (`Workspaces.remove(monitor, id)` for a shell): `RemoveDesktop(desktop,
  fallback)`, no visible switch. The windows go to `removalFallbackIndex(count, index)`: the left
  neighbour, the first desktop's to the right one; the last desktop is never closed. If the closed
  desktop was shown, Windows lands on the fallback. There is no keystroke fallback (Win+Ctrl+F4
  shows the desktop first), so without the shell interface `canRemove` is false.
- **Rename.** `rename(index, name)` calls `SetDesktopName`; the same rules as close.

## Shell interface

`win32/shell_desktops_win.*` talks to the interface behind Task View (`IVirtualDesktopManagerInternal`
in explorer.exe). It is undocumented and changes between builds, so it is optional and every step
fails soft. The GUIDs, slots and what was verified are in the allowlist rows of
`docs/architecture.md`; the declarations here are written from those facts.

- **Fail-soft chain.** `ShellDesktopApi::open` checks, in order: `kernel::liveDesktop("shell_desktops")`
  (false on the offscreen platform and with `--no-shell-desktops`), Windows build 26100 or later (older
  layouts are not declared), both interface keys under `HKCR\Interface`, `CoCreateInstance`
  (ImmersiveShell, `CLSCTX_LOCAL_SERVER`), `QueryService`, `QueryInterface`, and a read-only
  self-test against the registry: `GetCount`, `GetDesktops` ids in order, `GetCurrentDesktop`,
  and a `FindDesktop` round trip. Any failure logs one line, `virtual desktops: shell api unavailable:
  <reason>`, and the system works as before (anchor window, Win+Ctrl+D, polling). Slots are never
  scanned at run time.
- **Ids, not indexes.** The calls take desktop GUIDs from the registry list the model shows
  and resolve them with `FindDesktop`, so a lag between the registry and the shell cannot close
  another desktop than the one clicked.
- **Explorer restarts.** The proxy fails with 0x800706BA (also RPC call failed and disconnected). The
  object is dropped (`virtual desktops: shell api lost`) and reopened at most every 10 s from the next
  read, which also registers the notifications again. A hung Explorer blocks the GUI thread for
  the length of a call; there is no timeout on a COM call.
- **Change source.** The shell's notification sink and a `RegNotifyChangeKeyValue` watch on the
  VirtualDesktops key. The sink handlers only post a queued call to the GUI thread (Windows runs
  them inside outbound calls; calling the manager there would re-enter the STA). `DestroyBegin` also
  posts the ids of the closed desktop and of the one that gets its windows; they are logged at
  debug level, to compare with `removalFallbackIndex` against what Task View really uses.
- **Kill switch.** `--no-shell-desktops` (docs/building.md).
- **Risk.** The real hazard is a slot shift inside an unchanged IID (it happened on 22621.2215,
  22631.3085 and 26100). The self-test cannot catch a same-arity swap of two change methods. So
  the first change of a run (switch, create, close, rename) compares count, ids and the shown
  desktop with the registry again, logs at debug level, and refuses on a difference. Slots 9, 11, 13,
  16 and 22 were never executed on the machine where the layout was checked.
- **Tests.** The COM glue has no unit test; the rules around it do (`removalFallbackIndex`, remove
  guards, change-hook coalescing) on a fake `VirtualDesktopSystem`. No test calls the shell.

### Moving a window

`ShellDesktopApi::moveWindow(hwnd, desktopId)` does what Task View's "Move to" does:
`IApplicationViewCollection::GetViewForHwnd(hwnd)` (a service of ImmersiveShell, same GUID as its
interface) gives the window's `IApplicationView`, and `IVirtualDesktopManagerInternal::MoveViewToDesktop(view,
desktop)` (slot 4) moves it. The documented `IVirtualDesktopManager::MoveWindowToDesktop` is refused for
windows of other processes. The view is never called into; it is only handed back, so no
`IApplicationView` slot layout is declared. Verified on build 26300: a window of another process
moved to the other desktop and back, `GetWindowDesktopId` confirmed it, and the shown desktop did not
change. The fail-soft chain is: shell route, documented call, `Refused`. The first change of a run
re-verifies the manager against the registry as for switch and close.

### Open points

- A view exists only for a window the shell tracks. A window without a view (some tool windows)
  falls through to the documented call, which refuses it.

- Focus after `SwitchDesktop` is not guaranteed; the animated call is the default for that reason
  and for parity with the keyboard.
- Whether `GetDesktops` order equals the registry order is verified only for the count and for
  one desktop on the build above; the self-test refuses to open the interface otherwise.
- Notification delivery was not observed on the research machine; the registry watch covers it.
- The window lists are not covered by any notification; the 5 s poll (and `refresh()`) updates them.

## The `desktop` verb

`wakuc desktop goto <n> | move <n> | next | previous | status` (`desktop_command.h`; `VirtualDesktops`
implements `DesktopControl`). Numbers count from 1. Each command reads the registry list first.

- `goto`, `next`, `previous` make one jump with `jumpTo` and never create a desktop (`switchTo`, the
  QML call, still makes the next one). `next` and `previous` stop at the ends and answer `ok:
  unchanged`.
- `move <n>` sends the foreground window to desktop `n` with the documented
  `IVirtualDesktopManager::MoveWindowToDesktop`, using the GUID from the registry list. Nothing is
  created and the view does not follow. A window of waku, the desktop and the taskbar are refused.
  The shell route comes first (see "Moving a window"); the documented call is the fallback and
  only works for windows of the calling process. When both fail it answers `error: Windows refused
  ...` and logs the `HRESULT`.

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

## Managing komorebi

`--manage-komorebi` (off by default; the capability `manage_komorebi`) lets waku reload a running
komorebi after a display change. komorebi-bar is per-monitor DPI aware but handles no
`WM_DPICHANGED` or `WM_DISPLAYCHANGE` (v0.1.41 source), so after a dock or a scaling change the bar
can keep its old size. That is the observed symptom; the cause is not proven.

- **Command.** `komorebic replace-configuration <komorebi.json>`, the path from `komorebic
  configuration`. From komorebi's source (v0.1.41, `process_command.rs`): it rebuilds the window
  manager from the file and kills and respawns `komorebi-bar` once per `bar_configurations` entry,
  only when a bar already runs. `komorebic reload-configuration` is not used: it reloads legacy
  `.ahk` and `.ps1` files only. komorebic has no bar-only restart, and `stop --bar` stops komorebi
  too.
- **Run chain.** `cmd /c for /f "usebackq delims=" %i in (`komorebic.exe configuration`) do
  komorebic.exe replace-configuration "%i"` through `kernel::launchHiddenShell`: fire and forget, so
  a slow komorebic never blocks the GUI thread. No path is built in waku.
- **Condition.** Both `komorebi.exe` and `komorebi-bar.exe` run (`CreateToolhelp32Snapshot`). Without
  a bar the reload gains nothing and may move windows, so nothing is sent.
- **Trigger.** The settle of `KomorebiWorkAreaGuard` after a display change: `screenAdded`,
  `screenRemoved`, `primaryScreenChanged`, `geometryChanged`, `logicalDotsPerInchChanged`. A change
  of the available work area alone (waku's own bar registering) does not reload: it would run on
  every start. The work area nudge still follows, as before.
- **Cooldown.** After a reload no other one starts for 30 s, because the reload moves windows and
  the bar and so raises screen signals itself. A refused or failed start sets no cooldown.
- **Logs.** `qInfo` per reload (`komorebi manage: komorebic replace-configuration started`),
  `qDebug` once each time komorebi or its bar is missing, `qWarning` when komorebic cannot start.
- **Limits.** The config path is the one `komorebic configuration` prints (`KOMOREBI_CONFIG_HOME`
  or the home folder). A komorebi started with another `--config` is reloaded from that default
  file instead. replace-configuration pauses komorebi and restores every window before it rebuilds,
  so windows may visibly move. Not yet verified live with a bar running (open point).
- **Tests.** `tests/test_komorebi_manage.cpp` on `testing/fake_workarea.h`; no test starts komorebic.

## Windows APIs

Documented only: named pipes, `CreateToolhelp32Snapshot` for the process check, `MonitorFromWindow`,
WinHTTP WebSocket, `FindWindowW` and `SendMessageTimeoutW` (`WM_SETTINGCHANGE`, `SPI_SETWORKAREA`).
`winhttp` is linked by this feature alone.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- GlazeWM WebSocket thread (`std::thread`) (`GlazewmService` (`src/features/workspaces/glazewm_service.h`) via `glazewm::GlazewmSocket` (`src/features/workspaces/glazewm.h`, `win32/glazewm_socket.cpp`), created by the first widget subscription): owns the WinHTTP session, connect, blocking receive loop and the reconnect sleep (4 s, doubling to 30 s while GlazeWM does not listen); text frames and connection changes reach the GUI thread as queued signals; sends run on the GUI thread; `GlazewmSocket::stop()`: sets the stop event, cancels a blocked receive with `WinHttpWebSocketClose` (250 ms close handshake), `join`, then the handles are closed (idempotent, also from the destructor); the owner calls `GlazewmService::stop()`

- komorebi pipe thread (`std::thread`) (`KomorebiService` (`src/features/workspaces/komorebi_service.h`) via `komorebi::KomorebiPipeListener` (`src/features/workspaces/komorebi.h`, `win32/komorebi_pipe.cpp`)): owns the `\\.\pipe\waku-<uuid>` handle, runs `komorebic subscribe-pipe` and `state`, reads one JSON message per pipe message, reconnects; hands parsed state to the GUI thread with queued calls; `stop()`: sets the stop event (cancels waits, overlapped I/O and a running komorebic, which `CreateProcessW` started with drained output pipes and which is killed), `join`, then starts `komorebic unsubscribe-pipe` detached; started lazily by the first subscription; the owner calls it
