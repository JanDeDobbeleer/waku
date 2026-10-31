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
| `testing/wm_fixtures.h` | Sample window-manager state and harnesses on the services' test seams. |

## Threads

- komorebi pipe thread (`std::thread`, owned by `KomorebiPipeListener`): creates the pipe, runs
  `komorebic subscribe-pipe` and `state`, reads one JSON message at a time, reconnects. Results go
  to the GUI thread as queued calls. `stop()` sets the stop event, joins and unsubscribes.
- GlazeWM socket thread (`std::thread`, owned by `GlazewmSocket`): WinHTTP session, connect, blocking
  receive loop, 4 s reconnect sleep. Text frames and connection changes reach the GUI thread as queued
  signals; sends run on the GUI thread. `stop()` closes the handles and joins.

Both rows are in the thread table of `docs/architecture.md`. Everything else runs on the GUI thread.

## Behaviour

- Neither manager running is a normal state: `connected` stays false and `Workspaces.provider` is "".
- The komorebi monitor id is the `HMONITOR`; `Workspaces.monitors[].handle` matches
  `Window.window.monitorHandle`, so a bar shows the workspaces of its own screen.
- Commands (`focus`, layout toggles) are fire-and-forget hidden processes (`kernel::launchHiddenShell`
  for komorebic chains) so a slow `komorebic` never blocks the GUI thread.

## Windows APIs

Documented only: named pipes, `CreateToolhelp32Snapshot` for the process check, `MonitorFromWindow`,
WinHTTP WebSocket. `winhttp` is linked by this feature alone.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- GlazeWM WebSocket thread (`std::thread`) (`GlazewmService` (`src/features/workspaces/glazewm_service.h`) via `glazewm::GlazewmSocket` (`src/features/workspaces/glazewm.h`, `win32/glazewm_socket.cpp`), created by the first widget subscription): owns the WinHTTP session, connect, blocking receive loop and the 4 s reconnect sleep; text frames and connection changes reach the GUI thread as queued signals; sends run on the GUI thread; `GlazewmSocket::stop()`: sets the stop event, closes the WinHTTP handles, `join` (idempotent, also from the destructor); the owner calls `GlazewmService::stop()`

- komorebi pipe thread (`std::thread`) (`KomorebiService` (`src/features/workspaces/komorebi_service.h`) via `komorebi::KomorebiPipeListener` (`src/features/workspaces/komorebi.h`, `win32/komorebi_pipe.cpp`)): owns the `\\.\pipe\wabar-<uuid>` handle, runs `komorebic subscribe-pipe` and `state`, reads one JSON message per pipe message, reconnects; hands parsed state to the GUI thread with queued calls; `stop()`: sets the stop event (cancels waits and overlapped I/O), `join`, then `komorebic unsubscribe-pipe`; started lazily by the first subscription; the owner calls it
