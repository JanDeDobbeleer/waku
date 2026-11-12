# workspaces: the Windows virtual desktops and the Workspaces model

Current design of the capability. The widgets are `Workspaces` (with `WorkspaceDot`). The services
are `VirtualDesktops`, which reads and drives the Windows virtual desktops, and the `Workspaces`
singleton, a model of those desktops that a bar widget shows. waku supports no other workspace
provider: tiling is the built-in tiler (`features/tiling`).

## Layers

| Path | Content |
|---|---|
| `virtual_desktops.*` | `VirtualDesktopSystem` (the platform seam), the desktop list value types, `parseDesktopIds`, `removalFallbackIndex`. |
| `win32/virtual_desktops_win.cpp` | The real system: registry list, documented `IVirtualDesktopManager`, the shell interface, the anchor-window fallback. |
| `win32/shell_desktops_win.*` | `ShellDesktopApi`: the undocumented Task View interface (see "Shell interface"), and `RegistryChangeWatcher`. |
| `desktop_command.*` | The `desktop` verb of `wakuc` (`DesktopControl`). |
| `qml/virtual_desktops_service.*` | The `VirtualDesktops` QML singleton. |
| `qml/workspaces_service.*` | The `Workspaces` singleton: one `monitors` model over `VirtualDesktops`. |

## Threads

None. Everything runs on the GUI thread. The shell interface registers a notification sink whose
callbacks are marshalled to it.

## Behaviour

- `Workspaces.provider` is "windows", or "" when Windows exposes no desktop list.
- Virtual desktops belong to no monitor, so `Workspaces.monitors` has one entry with handle 0 that
  every bar shows (`m.handle === handle || m.handle === 0`).
- Commands (`focus`, `remove`) go to `VirtualDesktops`; the shell call is asynchronous enough that
  a slow Explorer does not block the GUI thread.

## Windows virtual desktops

`VirtualDesktops` (`qml/virtual_desktops_service.*`) is the provider; `Workspaces` maps it to
`provider: "windows"`.

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
- **Create.** `switchTo` with the index just past the last desktop (below 20) creates the next
  one and shows it. With the shell interface, `CreateDesktop` makes it without showing it and the
  service then jumps to it; without, one Win+Ctrl+D (the only call Windows documents) does both. A
  shell draws one spare dot for it (Celeste).
- **Close.** `remove(index)` (`Workspaces.remove(monitor, id)` for a shell): `RemoveDesktop(desktop,
  fallback)`, no visible switch. The windows go to `removalFallbackIndex(count, index)`: the left
  neighbour, the first desktop's to the right one; the last desktop is never closed. If the closed
  desktop was shown, Windows lands on the fallback. There is no keystroke fallback (Win+Ctrl+F4
  shows the desktop first), so without the shell interface `canRemove` is false.
- **Rename.** `rename(index, name)` calls `SetDesktopName`; the same rules as close.
- **Wallpaper on all desktops.** The root `setWallpaperForAllDesktops(path)` opens the guarded
  shell interface and calls `UpdateWallpaperPathForAllDesktops` (slot 18). The wallpapers feature
  uses it for Shift+Enter. It switches no desktop, writes no registry values and returns false
  when the shell API is disabled or refuses the change. The call has no fallback; the native
  mutation still needs owner validation on build 26300.9974.
- **Shell changes.** The edits that make a ported shell use the `windows` provider live with that
  shell, in its config home, not here (a ported shell under a copyleft licence stays out of this
  repository).

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
  created (except below) and the view does not follow. A window of waku, the desktop and the taskbar are refused.
  The shell route comes first (see "Moving a window"); the documented call is the fallback and
  only works for windows of the calling process. When both fail it answers `error: Windows refused
  ...` and logs the `HRESULT`.
- `move <n>` with `n` past the last desktop makes one new desktop, the next (`move 7` with three desktops
  makes the fourth, not three empty ones), and puts the window there. Below 20 desktops only.

## Windows APIs

Documented: the registry, `IVirtualDesktopManager`, `RegNotifyChangeKeyValue`, `SendInput` (the
create-desktop keystroke fallback). The undocumented shell interface is in the allowlist in
`docs/architecture.md`.
