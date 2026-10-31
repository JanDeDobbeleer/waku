# Engine-owned work area sync for komorebi

Date: 2026-10-31. Branch of record: `perf/lazy-surfaces` (engine), `main` of the caelestia port at
`D:\wabar-ports\caelestia` (commits `e633ff5`, `c31c343`). Line numbers are those of the files on
that day.

**Status: implemented** (2026-10-31), option C with the owner's answers to section 8:
`SendNotifyMessageW` behind a `WorkAreaNotifier` interface and no thread (1); no trigger on
`ShellHost::reloaded` (2); no trigger on a komorebi pipe connect, but a public
`KomorebiWorkAreaGuard::nudge()` for one later (3); the flag `--no-workarea-sync` with the
capability `workarea_sync` (4); no `ShellScreen` work area, commit 3 dropped (5); `ABN_POSCHANGED`
untouched (6). Differences from section 4: the interface is `WorkAreaNotifier::notify()` (not
`WorkAreaNudger`), the two timers sit behind a `WorkAreaTimers` interface so tests run on a fake
clock with the real 500 ms and 5 s, and the fakes are in `testing/fake_workarea.h`, not
`wm_fixtures.h`. The optional `Komorebi.refreshWorkArea()` invokable of option D was not added. The
live check of section 6 is open; its result goes into `src/features/workspaces/design.md`.

## Verdict

Move the fix into the engine as a small, komorebi-specific guard in the `workspaces` feature
(option C, revised): a GUI-thread `QObject` created by `main.cpp` that listens to Qt's own screen
signals (`QScreen::availableGeometryChanged`, which Qt emits from the very `WM_SETTINGCHANGE` that
Explorer broadcasts when the work area changes) and, after a short settle, sends
`WM_SETTINGCHANGE(SPI_SETWORKAREA)` to komorebi's hidden window with `SendNotifyMessageW`, once at
once and once 5 s later. It needs no `pwsh`, no process spawn, no `komorebic state` read, no thread,
no new singleton and no allowlist row, because komorebi's own handler already compares its cache
with the OS and retiles only on a difference, so the message is idempotent and the engine does not
have to decide whether it is needed. The port then deletes about 300 lines (Hypr.qml, the `.ps1`,
four DebugIpc verbs, a README paragraph). Estimated effort: 7 to 9 hours, engine and port together,
in three engine commits and one port commit. Options A (broadcast from `host`) and B (expose the work
area to QML and leave the policy to the shell) are rejected: A touches every top-level window for a
one-process problem, and B leaves komorebi's timing quirks (a bounded 20-slot channel, a 3 s
verification sleep) to every shell that runs on komorebi, which is exactly the kind of Windows
knowledge the engine exists to hold.

## 1. The problem and the workaround of today

komorebi (v0.1.41 on this machine, `komorebic --version`) caches each monitor's work area
(`work_area_size`) and refreshes it only from three window messages. After a wabar reload (the app
bar is removed and registered again) or a DPI and display storm (dock, scaling change) it can keep a
full-monitor work area, and no `komorebic` command re-reads the OS value. Windows then tile under the
bar.

The port works around it in QML (`modules/qs/services/Hypr.qml`, port `main`):

- Lines 229-265: the design comment, the state (`komorebiMonitors`, `osMonitors`,
  `lastAreaVerdict`, passes left) and `screensSig`, a string of every screen's geometry and DPR;
  `onScreensSigChanged: startAreaCycle()` (line 265).
- Lines 271-302: `workAreaRows`, a 4 px tolerance compare of komorebi's rects with the OS rects,
  with the states `ok`, `differs`, `stale` (komorebi's `HMONITOR` is gone) and `unknown`.
- Lines 307-321: `refreshKomorebi`, which starts the `areaNotify` process.
- Lines 327-332: `startAreaCycle`: a pass 2.5 s after the last screen change and a second one 6 s
  later; at most two refreshes per cycle, none within 5 s of the last (lines 348-380).
- Lines 613-630: `parseState` reads `monitors[].work_area_size` from a fresh `komorebic state`.
- Lines 937 and 1038: cycles start after a start or reload.
- Lines 1042-1097: the `areaTimer`, the `areaQuery` process (`pwsh ... komorebi-workarea.ps1
  -Query`) and the `areaNotify` process (`-Notify`).
- `modules/qs/services/komorebi-workarea.ps1`: `Add-Type` C# with `EnumDisplayMonitors`,
  `GetMonitorInfo`, `GetDpiForMonitor`, `FindWindow("komorebi-hidden", "komorebi-hidden")` and
  `SendMessageTimeout(WM_SETTINGCHANGE, SPI_SETWORKAREA, SMTO_ABORTIFHUNG, 2000 ms)`.
- `modules/qs/modules/bar/DebugIpc.qml` lines 401-480: `komorebiAreas`, `komorebiRefresh`,
  `komorebiCycle`, `komorebiAreasTest`. `README.md` lines 61-70 describe it.

Cost of that design: two `pwsh` starts per pass (each compiles C# with `Add-Type`: hundreds of
milliseconds and a visible CPU spike), a `komorebic state` dump per pass, a script path resolved from
a QML URL, and komorebi internals spelled out in a shell that is supposed to be configuration.

## 2. What the engine knows, and when

### 2.1 The app bar flow in wabar

- `src/host/qml/shell_host.cpp:751-792` (`syncAppBar`): builds an `AppBarRequest` from the
  `PanelWindow` (edge, `reservedZone()`, screen, `allowReserve && zone > 0`) and calls
  `IAppBar::create` or `reposition`. It returns at once with the inset; nothing waits for the work
  area.
- `src/host/win32/app_bar.cpp:40-59` (`create`): `ABM_NEW` with
  `uCallbackMessage = kAppBarCallbackMessage` (`src/host/app_bar.h:14`), then `negotiate`:
  `ABM_QUERYPOS` and `ABM_SETPOS` (`src/host/app_bar.cpp:64-77`), rects in physical pixels
  (`physicalScreenRect`, `src/host/app_bar.cpp:8-13`). `remove()` sends `ABM_REMOVE`
  (`src/host/win32/app_bar.cpp:67-71`).
- A reload removes every app bar first: `ShellHost::Loaded::~Loaded`
  (`src/host/qml/shell_host.cpp:164-172`) calls `appBar->remove()`, and `resetEngine()` runs before
  `build()` (`shell_host.cpp:449-453`); the new shell's windows register again in `applyNative` and
  `syncAppBar` after `install` (`shell_host.cpp:555-596`, `708-735`). So every reload is one
  `ABM_REMOVE` and one `ABM_NEW`/`ABM_SETPOS` per bar: two work area changes.
- Screen changes reload the shell after a 1.5 s debounce (`watchScreens`,
  `shell_host.cpp:382-404`: `screenAdded`, `screenRemoved`, `primaryScreenChanged`,
  `logicalDotsPerInchChanged`).
- The callback message is set but never handled: `kAppBarCallbackMessage` has one use
  (`src/host/win32/app_bar.cpp:55`), no `ABN_POSCHANGED` handler exists (grep over `src/`). Windows
  sends `ABN_POSCHANGED` to wabar's bar when the taskbar or another app bar moves; wabar ignores
  it. That is a separate gap, not needed for this plan.
- The engine has no model of the OS work area: `ShellScreen` (`src/host/qml/shell_screen.h:22-60`)
  mirrors `geometry()` and `devicePixelRatio()` only. The one use of `availableGeometry` is the
  centring of a `FloatingWindow` (`src/host/qml/shell_window.cpp:663`).

### 2.2 What Windows does with an app bar

From the Microsoft Learn pages (quoted by a fetch on 2026-10-31):

- `ABM_SETPOS` and `ABM_REMOVE`: "This message causes the system to send the `ABN_POSCHANGED`
  notification message to all appbars." The appbar pages never mention the work area or
  `WM_SETTINGCHANGE`.
- `SPI_SETWORKAREA`: "Sets the size of the work area. The work area is the portion of the screen
  not obscured by the system taskbar or by application desktop toolbars. [...] In a system with
  multiple display monitors, the function sets the work area of the monitor that contains the
  specified rectangle." `SPI_GETWORKAREA` returns physical pixels; per-monitor values come from
  `GetMonitorInfo` (`rcWork`).
- `WM_SETTINGCHANGE`: "A message that is sent to all top-level windows when the
  `SystemParametersInfo` function changes a system-wide setting [...]. To send the
  `WM_SETTINGCHANGE` message to all top-level windows, use the `SendMessageTimeout` function with
  the `hwnd` parameter set to `HWND_BROADCAST`."

The link between the two is not documented but is relied on by everyone: Explorer's app bar
manager sets the work area through `SPI_SETWORKAREA` and broadcasts `WM_SETTINGCHANGE` with
`wParam == SPI_SETWORKAREA`. Qt's platform plugin comments it ("Among other things,
`WM_SETTINGCHANGE` happens when the taskbar is moved and therefore the 'working area' changes",
`qtbase/6.10/src/plugins/platforms/windows/qtwindowsglobal.h`), GlazeWM's display listener comments
it ("fires on taskbar or appbar changes"), and komorebi handles it. The work area is applied on
Explorer's thread, after `SHAppBarMessage` returns: wabar already works around that asynchrony in
`waitForFullWorkArea` (`src/host/win32/taskbar_control.cpp:102-113`), which polls
`GetMonitorInfoW(...).rcWork` for up to 0.5 s after `ABM_SETSTATE`.

So the moment a reservation has settled is the moment Explorer broadcasts `WM_SETTINGCHANGE`. The
engine already receives that broadcast: every bar window is a top-level window, and Qt 6.10 handles
it for the whole process (`qwindowscontext.cpp`, `QWindowsContext::windowsProc`):

```cpp
case QtWindows::SettingChangedEvent: {
    QWindowsWindow::settingsChanged();
    return d->m_screenManager.handleScreenChanges();
}
```

`handleScreenChanges` re-enumerates the monitors; `monitorData` fills
`data->availableGeometry` from `info.rcWork`; `QWindowsScreen::handleChanges` compares
`availableGeometry` with the cached value and calls
`QWindowSystemInterface::handleScreenGeometryChange(screen, geometry, availableGeometry)`, from
which `QScreen::availableGeometryChanged` is emitted (documented signal). `WM_DISPLAYCHANGE` takes
the same path through a hidden top-level observer window (`qDisplayChangeObserverWndProc`). No
`wParam` filter: any `WM_SETTINGCHANGE` costs one re-enumeration, which is what makes the signal
reliable here.

Conclusion for (1): the engine knows that the OS work area changed, per screen and without
polling, through `QScreen::availableGeometryChanged`; it knows the rect in logical pixels and can
convert it (`physicalScreenRect`, `src/host/app_bar.cpp:8-13`). It does not know whether komorebi
received the broadcast.

### 2.3 What komorebi does with the broadcast, and why it misses

komorebi v0.1.41, `komorebi/src/monitor_reconciliator/hidden.rs` and `mod.rs` (fetched at tag
`v0.1.41`):

- A dedicated thread owns a hidden top-level window of class `komorebi-hidden`
  (`Hidden::create("komorebi-hidden")`). Its window procedure maps `WM_SETTINGCHANGE` with
  `wparam == SPI_SETWORKAREA` to `WorkAreaChanged`, `WM_DISPLAYCHANGE` to
  `ResolutionScalingChanged`, `WM_DEVICECHANGE` with `DBT_DEVNODES_CHANGED`, `DBT_CONFIGCHANGED`,
  `DBT_DEVICEARRIVAL` or `DBT_DEVICEREMOVECOMPLETE` to `DisplayConnectionChange`, and power and
  session messages to the suspend and lock variants. It sleeps 10 ms after each message.
- Delivery is `try_send` on `crossbeam_channel::bounded(20)`; a full channel logs
  `channel is full; dropping notification` and drops the event.
- One consumer thread handles the channel. `WorkAreaChanged`:

```rust
if let Ok(reference) = WindowsApi::monitor(monitor.id)
    && reference.work_area_size != monitor.work_area_size
{
    monitor.work_area_size = Rect { ... reference.work_area_size ... };
    should_update = true;
}
if should_update {
    monitor.update_focused_workspace(offset)?;
    border_manager::send_notification(None);
}
```

  `WindowsApi::monitor(hmonitor)` walks `win32_display_data::connected_displays_all()` and matches
  `display.hmonitor`; `work_area_size` is `GetMonitorInfoW(...).rcWork` unchanged (crate
  `win32-display-data`, `device.rs`). A lookup that fails (stale `HMONITOR`) is a silent no-op.
  `offset` is the global `work_area_offset`; per-monitor and per-workspace offsets stay in
  `update_focused_workspace`, so the nudge never bypasses a user's padding.
- `DisplayConnectionChange` (also resume and unlock) re-reads the attached devices. When the count
  dropped it logs "potential monitor removal detected ... verifying in 3s", sleeps 3 s on the
  consumer thread, re-queries, then either restores or caches the monitor and calls
  `load_monitor_information`.
- `process_command.rs` has no message that re-reads the OS work area: `Retile` and
  `RetileWithResizeDimensions` tile against the cache, `WorkAreaOffset` stores a client value,
  `ReloadConfiguration` runs the config script. `ReplaceConfiguration` calls `wm.init()` (not
  verified whether that re-enumerates).

Why the broadcast is missed, in order of likelihood:

1. Dropped. A dock or a scaling change produces a burst of `WM_DEVICECHANGE` (one per device node),
   `WM_DISPLAYCHANGE` and `WM_SETTINGCHANGE`. Each is one slot; while the consumer sleeps 3 s in
   the removal check, 20 slots fill, and the `WorkAreaChanged` that wabar's re-registration causes
   1.5 s plus later (the host debounce, then the reload) is the one that arrives last and is
   dropped.
2. Overwritten. komorebi's own `ResolutionScalingChanged` and `DisplayConnectionChange` handlers
   read `rcWork` when they run. If that is between wabar's `ABM_REMOVE` and its `ABM_NEW` (a
   reload takes hundreds of milliseconds; the Variants windows are destroyed first), komorebi
   caches the full monitor. The later broadcast would correct it, unless it is dropped (1) or the
   handle is stale (3).
3. Stale handle. After a dock the `HMONITOR` of a display can change. `WorkAreaChanged` looks the
   monitor up by `id` and does nothing when it is gone; only `DisplayConnectionChange` rebuilds the
   list. The port reports this as `stale` and cannot fix it either.
4. Handler abort. `update_focused_workspace(offset)?` returns the error out of the handler loop; the
   outer loop restarts it, and events in flight are lost.

What the engine can add that Windows does not: a late, targeted retry. Windows sends one broadcast
at the moment of the change; nobody re-sends it after komorebi's channel has drained or its 3 s
sleep has ended. komorebi's handler compares before it acts, so a second and third message cost one
`GetMonitorInfoW` per monitor and nothing else. The engine can also pick the target window instead
of the whole desktop.

### 2.4 GlazeWM

GlazeWM 3.10.1 (installed here) does not have the problem. Its hidden top-level window (class
`MessageWindow`, `platform_impl/windows/event_loop.rs`) also maps `WM_SETTINGCHANGE` with
`SPI_SETWORKAREA` to a display-settings event (`display_listener.rs`), but the channel is unbounded,
`working_area()` calls `GetMonitorInfoW` on every read (`display.rs`, no cache at the platform
layer), and `handle_display_settings_changed.rs` redraws the whole tree on every event. A targeted
message to a window of class `MessageWindow` is not safe (the name is generic). GlazeWM stays out of
scope; the design keeps a list of targets so one can be added if a later version regresses.

### 2.5 Quickshell

Quickshell gives QML no work area. `ShellScreen` (`src/core/qmlscreen.hpp`) exposes `name`,
`model`, `serialNumber`, `x`, `y`, `width`, `height`, `physicalPixelDensity`,
`logicalPixelDensity`, `devicePixelRatio`, `orientation`, `primaryOrientation`; it never reads
`availableGeometry`. `PanelWindow` (`src/window/panelinterface.hpp`) has `exclusiveZone`
("The amount of space reserved for the shell layer relative to its anchors") and `exclusionMode`
(`Normal`: "Respect the exclusion zone of other shell layers and optionally set one"; `Ignore`;
`Auto`: "Decide the exclusion zone based on the window dimensions and anchors"), all about the
window's own zone. The X11 backend writes `_NET_WM_STRUT_PARTIAL` and computes the usable area from
other Quickshell panels only, never from `_NET_WORKAREA`. `HyprlandMonitor.lastIpcObject` is a
snapshot of `hyprctl monitors -j` (which carries `reserved`) that refreshes only on
`Hyprland.refreshMonitors()`. wabar's `PanelWindow` mirrors the same surface
(`src/host/qml/shell_window.h:402-470`). An `availableGeometry` on `ShellScreen` would be a wabar
extension, generic to every Windows shell, not a compatibility item.

## 3. Options

Effort is engineering time for the engine change, tests and docs; the port cleanup (1 h) is the same
for A to D.

### A. Generic re-broadcast from `host`

After every app bar create, reposition or remove and after every screen change settles, `host`
sends `WM_SETTINGCHANGE(SPI_SETWORKAREA)` either to a list of known window manager windows or to
`HWND_BROADCAST` as Explorer does.

- Broadcast: every top-level window re-reads its settings (Explorer, browsers, Office, Qt itself
  re-enumerates the monitors). wabar did not change a setting, so the broadcast is a lie, and the
  taskbar reacts to work area changes by showing an auto-hide taskbar again
  (`src/host/win32/taskbar_control.cpp:125-127`). `SendMessageTimeout(HWND_BROADCAST)` waits up to
  the timeout per hung window; `SendNotifyMessage` avoids the wait but not the storm. Not safe.
- Targeted list in `host`: `host` would then know komorebi's window class, which is window manager
  knowledge that lives in `workspaces` today (`komorebi.h`, `design.md`). It would also need the
  settle timing of a program `host` knows nothing about. Wrong layer.
- Effort 4 h. Risk: medium (broadcast) or layering debt (targeted).

### B. Expose the work area to QML and let the shell decide

`ShellScreen` gains `availableX/Y/Width/Height` (or one `availableGeometry` rect) with a
`workAreaChanged` signal, plus an invokable the shell calls to send the message, because QML cannot
send a window message.

- Keeps komorebi policy in the config, which is the rule's intent, but the policy is not shell
  policy: the timing comes from komorebi's channel size and 3 s sleep, and every shell that runs on
  komorebi (there will be more than one port) must copy it. The one thing QML cannot do, the
  `SendMessage`, has to be an engine invokable anyway, so B is C with the trigger moved out.
- The `availableGeometry` part is cheap and useful on its own (popups and floating windows inside
  the work area; a shell can show the reserved strip). It is a good follow-up, not a fix.
- Effort 3 h for the properties and tests, plus whatever each shell writes. Risk: low for the
  engine, high for consistency across shells.

### C. A komorebi work area guard in `workspaces` (recommended, revised)

A root-level class `KomorebiWorkAreaGuard` (Qt Core and Gui only) in
`src/features/workspaces/`, with the Win32 sender in `win32/`. Trigger: `QScreen` signals
(`availableGeometryChanged`, `geometryChanged`, `logicalDotsPerInchChanged`, `screenAdded`,
`screenRemoved`), debounced 500 ms to coalesce the remove and add of a reload, then one nudge at
once and one 5 s later (komorebi's 3 s verification plus margin); a new burst re-arms the pair. The
nudge is `FindWindowW(L"komorebi-hidden", nullptr)` and
`SendNotifyMessageW(hwnd, WM_SETTINGCHANGE, SPI_SETWORKAREA, 0)`.

Revisions to the C of the brief, with reasons:

- No `komorebic state` read and no compare. komorebi compares for us
  (`reference.work_area_size != monitor.work_area_size`) and logs "work areas match,
  reconciliation not required" otherwise. The port's compare existed to avoid a `pwsh` spawn, not
  to protect komorebi. Dropping it removes the pipe thread question, the JSON parsing of
  `work_area_size`, the DPI conversion and the 4 px tolerance.
- Not on the pipe thread, not on a worker. `SendNotifyMessageW` returns at once for a window of
  another thread ("passes the message to the window procedure and returns immediately"), and
  `WM_SETTINGCHANGE` (`0x1A`) is below `WM_USER`, so no marshalling is involved with `lParam == 0`.
  `FindWindowW` is a quick user32 call. The GUI thread does both. If the owner wants a delivery
  result, `SendMessageTimeoutW(SMTO_ABORTIFHUNG, 1000 ms)` on a `QThreadPool` task is the
  alternative, with one row in the thread table.
- Triggered by Qt's screen signals, not by a `host` hook. The feature cannot include `host`
  headers (rule S2, `tools/check.ps1:183`), and the signal Qt emits is the OS truth: it fires when
  Explorer has applied the work area, whoever caused it (wabar's bar, the taskbar, another app
  bar, a dock). A `host` hook would fire before Explorer applied it.
- Owned by `main.cpp`, not by the `Komorebi` QML singleton. The singleton is created on first use
  by a widget (`docs/architecture.md:410`) and destroyed on every reload
  (`architecture.md:422-454`); a shell that uses `komorebic` directly (the caelestia port does for
  its layout button) would never create it, and a reload would lose the pending 5 s nudge. The
  guard lives for the process like `WabarTaskbar` (`main.cpp:135-146`) and `host::Hotkeys`.
- Switch: `--no-workarea-sync`, implemented as `kernel::setCapabilityDisabled("workarea_sync")`
  next to `tray` and `hotkeys` (`main.cpp:104-105`); the real sender exists only when
  `kernel::liveDesktop("workarea_sync")`. The name is generic in case a second target is added.

Effort 5 to 7 h. Risk: low. The message is the one Explorer sends, to the one window komorebi
created to receive it, and komorebi ignores it when nothing changed.

### D. Combinations

C now, B's `availableGeometry` as a follow-up for shells. A is not combined with anything. A
`Komorebi.refreshWorkArea()` invokable (the same sender, through the singleton) comes almost free
with C and gives shells and `wabarc` a manual trigger for diagnostics.

### E. Do nothing in the engine

Keep the port workaround. Costs: two `pwsh` starts with `Add-Type` per pass (four per settle
cycle), a `komorebic state` dump per pass, 230 lines of QML plus 78 of PowerShell per shell, a
`.ps1` beside the shell's QML, an execution-policy bypass, a dependency on `pwsh` being on `PATH`,
and the same code again in the next komorebi shell. The port also cannot see komorebi's pipe
state, so it reads it through a child process. Nothing in the engine changes, which is the only
argument for E.

## 4. The recommended design in detail

### 4.1 Where the code lives

| File | Unit and folder | Content |
|---|---|---|
| `src/features/workspaces/komorebi_workarea.h`, `.cpp` | feature root (Qt Core and Gui; rule S1, S3) | `WorkAreaNudger` interface (`virtual NudgeResult nudge() = 0;`), `NudgeResult {bool windowFound; bool sent; quintptr hwnd;}`, class `KomorebiWorkAreaGuard : QObject` with the screen watching, the debounce and the two timers; constructor takes `std::unique_ptr<WorkAreaNudger>` and a `Timing {int settleMs = 500; int lateMs = 5000;}` |
| `src/features/workspaces/win32/komorebi_workarea_win.cpp` | `win32/` (`_win` suffix: same name as the root header, `AGENTS.md` "Adding a feature" step 1) | `makeWin32WorkAreaNudger()`: `FindWindowW`, `SendNotifyMessageW`; logs `ERROR_ACCESS_DENIED` once (komorebi elevated, UIPI) |
| `src/features/workspaces/testing/wm_fixtures.h` | `testing/` | `FakeWorkAreaNudger` recording calls and returning a scripted result |
| `src/features/workspaces/tests/test_komorebi_workarea.cpp` | `tests/` | suite `KomorebiWorkArea...` |
| `src/features/workspaces/CMakeLists.txt` | | the five new entries in `SOURCES`, `WIN32_SOURCES`, `TESTS` |
| `src/app/wabar/main.cpp` | `app` (may include every unit) | the flag, the capability, the guard object created after the control pipe is taken and before the first reload, parented to nothing and destroyed with `main` |
| `src/features/workspaces/design.md`, `docs/architecture.md` | docs | see 4.3 |

Dependency direction holds: `app -> features/workspaces -> kernel`. The guard uses no `host` type.
The kernel three-test rule is not met (one user, domain knowledge "komorebi"), so nothing goes to
`kernel/`. No QML file, so `wabar.widgets` and `wabar.services` are untouched unless D's invokable is
added (then `qml/komorebi_client.h` gains `Q_INVOKABLE bool refreshWorkArea()` that calls the same
`makeWin32WorkAreaNudger()` when `liveDesktop("workarea_sync")`).

The komorebi window class `komorebi-hidden` is a komorebi implementation detail, not a Windows API.
It goes into `design.md` with the version it was read from (v0.1.41, `monitor_reconciliator/mod.rs`,
`Hidden::create("komorebi-hidden")`) and the failure mode when it is renamed (nothing found, nothing
sent, one log line). The port looked the window up by class and title; the title is not confirmed
in the source, so the engine matches the class alone.

### 4.2 API surface

- Command line: `--no-workarea-sync` ("Never nudge a tiling window manager after the work area
  changed."). Default on.
- Capability: `kernel::liveDesktop("workarea_sync")`.
- Logging: `qInfo` per nudge (`komorebi work area nudge sent to 0x...`), `qWarning` once per run
  when the send is refused, `qDebug` when no window exists.
- Optional (D): `Komorebi.refreshWorkArea()` on the QML singleton; `wabarc` gets nothing new (a
  shell IPC handler can call the invokable).
- Nothing in the config home. Shells need no change beyond deleting their workaround.

### 4.3 Rows and notes for `docs/architecture.md`

- Threading table: no row. The guard runs on the GUI thread, owns no thread and no timer beyond two
  single-shot `QTimer`s that are inactive when idle.
- Singleton table: no row. It is one object owned by `main`, like `WabarTaskbar`; a paragraph
  under a new heading "komorebi's work area" next to "The Windows taskbar" (`architecture.md:307`)
  describes the trigger, the message, the flag and the inertness off the live desktop.
- Undocumented API allowlist: no row. `FindWindowW`, `SendNotifyMessageW`, `WM_SETTINGCHANGE` and
  `SPI_SETWORKAREA` are documented; the class name is documented in `design.md` as a komorebi
  detail with its fallback. If the `SendMessageTimeout` variant is chosen instead, still no row.
- Dependency table (`architecture.md:52`): unchanged (`workspaces` depends on no other feature).
- `AGENTS.md`: unchanged (scope `workspaces` exists).

### 4.4 Failure modes

| Case | Behaviour |
|---|---|
| komorebi not running | `FindWindowW` returns null; nothing sent; one `qDebug`. No process scan is needed, so no `CreateToolhelp32Snapshot`. |
| komorebi restarted | Its start calls `load_monitor_information`, which reads `rcWork` fresh, so it is already correct. If it restarts inside a storm, the late nudge still reaches the new window. Optional: a nudge on pipe `connect`, if the owner wants it (open question 3). |
| wabar started before or after komorebi | Both covered: wabar's first registration changes the work area and triggers a nudge; komorebi starting later reads the current `rcWork`. |
| Multiple monitors | One message; komorebi's handler iterates every monitor. |
| Monitor handle changes (dock) | komorebi's `WorkAreaChanged` cannot find the old `HMONITOR` and does nothing. The nudge cannot fix it; only komorebi's `DisplayConnectionChange` path can, and it runs on the same storm. The guard's late nudge lands after that path (3 s sleep plus re-query), which is the best an outside process can do. A diagnostic that compares the pipe state's monitor ids with the OS handles (`monitorFromWindow`, `komorebi.h:40`) is possible later. |
| DPI virtualisation | Irrelevant to the message: no rectangle is sent; komorebi reads `rcWork` itself as a per-monitor-aware process, and so does Qt. Only the optional diagnostic compare needs `physicalScreenRect`. |
| `work_area_offset`, per-monitor and per-workspace padding | Applied by komorebi inside `update_focused_workspace(offset)` after the cache refresh; the nudge does not bypass them. The user's `D:\windows-rice\komorebi\komorebi.json` sets none (read-only check). |
| komorebi elevated (UIPI) | `SendNotifyMessageW` fails with `ERROR_ACCESS_DENIED`; logged once; the user can run both at the same integrity level. |
| komorebi renames or drops the hidden window | nothing found, nothing sent; the port had the same exposure. |
| A hung komorebi | `SendNotifyMessageW` does not wait, so the GUI thread is never blocked. |
| Qt does not emit the signal | If `rcWork` did not change in Qt's eyes (same rect before and after a remove and add within one `handleScreenChanges`), there is no trigger. A reload removes and adds in two separate broadcasts, so this needs the two to coalesce in Explorer. Mitigation: `geometryChanged` and `logicalDotsPerInchChanged` are triggers too, and the host's reload path can be a third trigger later through `main.cpp` (`ShellHost::reloaded`), which `app` may connect. |
| GlazeWM | Not targeted; it re-reads on its own. |

### 4.5 Testability

- `KomorebiWorkAreaGuard` takes the nudger and the timing by value; tests pass `FakeWorkAreaNudger`
  and `{settleMs = 10, lateMs = 30}` and call the public slot `screensChanged()` (the same slot the
  `QScreen` connections use) instead of waiting for a real screen.
- Tests, all under `KomorebiWorkArea*` so `ctest -R KomorebiWorkArea` selects them: one burst of
  five calls yields exactly two nudges (at once after settle, then late); a second burst during the
  late wait re-arms and still yields two in total for that burst; a nudger that finds no window
  yields no warning spam (one log per state change); a disabled capability builds no nudger and the
  guard is inert; `main.cpp` wiring is covered by the existing start-up tests only through the flag
  parsing, as `--no-tray` is.
- The Win32 sender is not unit tested (live desktop only), as `makeWin32AppBar` and
  `makeWin32TaskbarSystem` are not.

### 4.6 Live-desktop rules

- The real nudger is created only when `kernel::liveDesktop("workarea_sync")`; the offscreen
  platform (every `ctest` run) gets none, so no test ever sends a message to another process.
- No test starts wabar against a real config (`--config-home` as always); the guard reads nothing
  from the config home.
- `--safe` keeps the guard: it is about the desktop, not the config.

### 4.7 Performance

No polling. Idle cost is zero: two inactive single-shot timers and a handful of signal connections.
A burst costs two `FindWindowW` and two `SendNotifyMessageW`. komorebi's cost per nudge is one
`EnumDisplayMonitors` pass and a compare per monitor; a retile happens only when the cache was
wrong, which is the case the user wants fixed. Compared with the port: no `pwsh`, no C# compile, no
`komorebic state` dump, no JSON parse.

## 5. Work breakdown

| # | Commit | Files | Tests | Hours |
|---|---|---|---|---|
| 1 | `feat(workspaces): nudge komorebi after the work area settles` | `komorebi_workarea.h/.cpp`, `win32/komorebi_workarea_win.cpp`, `testing/wm_fixtures.h`, `CMakeLists.txt`, `design.md`, `docs/architecture.md` (new paragraph) | `tests/test_komorebi_workarea.cpp` (4 cases above) | 3.5 |
| 2 | `feat(app): own the work area guard and add --no-workarea-sync` | `src/app/wabar/main.cpp`, `docs/building.md` (flag list, if it lists flags) | flag parsing as for `--no-tray` | 1.0 |
| 3 | optional, `feat(host): expose the work area of a ShellScreen` (option B as a follow-up) | `shell_screen.h/.cpp` (`availableX/Y/Width/Height`, `availableGeometryChanged` folded into `geometryChanged` or its own signal), `docs/architecture.md` QML types | `src/host/tests/test_screen_match.cpp` or a new `test_shell_screen.cpp`: the rect follows a fake `QScreen` | 1.5 |
| 4 | port, `refactor(shell): drop the komorebi work area workaround` | see section 7 | the port's `komorebiAreasTest` goes with it | 1.0 |
| | live verification (section 6) and the `verified on` note in `design.md` | | | 1.5 |

Total: 7 h without commit 3, 8.5 h with it. Delegation per the owner's workflow: commits 1 and 2
are standard work (Sonnet), commit 4 is mechanical (Haiku), commit 3 standard. `tools\check.ps1`
green before each commit; the arm64-debug preset builds the new sources with `/W4 /WX`.

## 6. What to measure live, and how

Read-only on komorebi except through the nudge itself. Before each step, record
`komorebic state` (`monitors.elements[].{name,id,size,work_area_size}`) and the OS value
(`Get-CimInstance` does not give `rcWork`; the port's `komorebi-workarea.ps1 -Query` still works as
a read-only probe until it is deleted, or the optional `ShellScreen` rect of commit 3).

1. Reload: `wabarc --config-home <home> reload` with `--register-app-bar`. Expect: komorebi's
   `work_area_size.top` (top bar) equals the bar's physical thickness within 1 s of the reload; the
   wabar log shows two nudge lines per burst; `%LOCALAPPDATA%\komorebi\komorebi.log` shows
   `updated work area for <device>` or `work areas match`.
2. Scaling change (Settings, 150 % to 125 % and back) on the bar's monitor. Expect the same match
   within 7 s (1.5 s host debounce, reload, 0.5 s settle, 5 s late nudge). Look for
   `channel is full; dropping notification` in komorebi's log: it shows the drop that motivated the
   late nudge.
3. Plug and unplug a display. Expect a match on every remaining monitor; a `stale` case (komorebi's
   `id` differs from the OS handle for the same name) is logged by komorebi as a no-op and is the
   known limit (section 4.4).
4. komorebi restart while wabar runs (`komorebic stop`, `komorebic start`, both the user's own
   commands). Expect a correct work area without a nudge, and no error from the guard.
5. `--no-workarea-sync`: repeat step 1 and expect the old behaviour (komorebi may keep the full
   monitor) and no nudge lines.
6. Idle: Task Manager shows no periodic CPU from wabar attributable to the guard (no timers run).

Record the Windows build (`10.0.26300` here), komorebi `0.1.41` and the dates in `design.md`.

## 7. Migration: what the port deletes afterwards

In `D:\wabar-ports\caelestia` (one commit):

- `modules/qs/services/Hypr.qml`: lines 229-265 (design comment, state, `screensSig`), 271-302
  (`workAreaRows`, `areaNeedsRefresh`), 307-321 (`refreshKomorebi`), 327-380 (`startAreaCycle`,
  `areaPass`, `debugAreaSkew`, `checkWorkArea`), the `kMons`/`komorebiMonitors`/`areaCheckPending`
  lines of `parseState` (613-630), the `startAreaCycle()` calls at 937 and 1038, the `areaTimer`,
  `areaQuery` and `areaNotify` blocks (1042-1097), and the sentence at line 205 ("The only other
  thing sent to komorebi is the work area message of refreshKomorebi."). The `dryRun` property stays
  if other code uses it.
- `modules/qs/services/komorebi-workarea.ps1`: delete.
- `modules/qs/modules/bar/DebugIpc.qml`: `komorebiAreas`, `komorebiRefresh`, `komorebiCycle`,
  `komorebiAreasTest` (lines 401-480).
- `README.md` lines 61-70: replace with one sentence: the engine keeps komorebi's work area in sync
  (`--no-workarea-sync` turns it off).
- Keep nothing: the port's `stale` detection has no action attached.

Order: engine commits 1 and 2 ship first and are verified live (section 6); the port commit follows
in the same session so the user never runs both mechanisms at once (two nudges plus the port's two
refreshes are harmless, but the logs would mislead).

> Implementation note (live run, 2026-10-31): `SendNotifyMessageW` is refused with error 1159
> (`ERROR_MESSAGE_SYNC_ONLY`) for WM_SETTINGCHANGE, so question 1 resolved to
> `SendMessageTimeoutW` (250 ms, `SMTO_ABORTIFHUNG`) on the GUI thread.

## 8. Open questions for the owner

1. `SendNotifyMessageW` (no wait, no result) or `SendMessageTimeoutW` on a pool task (a result,
   one thread-table row)? The plan assumes the first.
2. Should the guard also fire on `ShellHost::reloaded` (wired in `main.cpp`), as a belt for the
   case where Qt sees no `availableGeometry` change? The plan leaves it out until a live run shows
   the need.
3. Should a komorebi pipe `connect` (the `Komorebi` singleton exists) trigger a nudge? Only useful
   when komorebi restarts inside a storm; the plan leaves it out.
4. Is `--no-workarea-sync` the right name, or should it be `--no-komorebi-sync` to match the
   target? The generic name is chosen so a second target needs no new flag.
5. Do you want commit 3 (`ShellScreen` work area for QML) in this round? It is independent and
   useful for popup placement, but it is not part of the fix.
6. Should the plan also handle `ABN_POSCHANGED` (section 2.1, an unrelated gap: wabar does not
   re-negotiate when the taskbar moves)? Out of scope here; worth its own note.
