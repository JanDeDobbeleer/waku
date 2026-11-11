# waku taskbar: design note

Status: built. Sits next to the Tray widget (`src/features/tray/design.md`). Reference: Python YASB
`yasb/taskbar*` (branch `local/build`); the Qt Widgets taskbar that preceded waku is at the git tag
`widgets-main-final`.

## What the widget does

One button per running window of this monitor, or per app when grouping is on. A button shows the
window's icon, a count badge for a group of two or more, and a tint for the active window, a
minimised window and a window that flashes for attention. Pinned apps stay in the bar as buttons
when they do not run and take the place of their window when they do.
`grouping` is off by default, as in Python's schema (`taskbar.grouping.enabled: false`); a user config that
turns it on in Python must set `grouping: true` here. The group key is the app's unique id (AUMID, else
`path:<exe>`, as Python's `get_app_unique_id`), so two windows of one exe group even without an AUMID;
a window with no id never groups.

- Left click: focus the window; minimise it when it is already active; restore it when minimised;
  on a group, activate the next window of the app after the active one (cycle).
- Middle click: close the window (the app's first window for a group). A pinned app that does not
  run is launched instead.
- Right click: a menu in a `PopupWindow`: close window, close all windows (groups only), pin to or
  unpin from the taskbar. A pinned app that does not run gets "Open app" and "Unpin".
- Hover: the window title as a tooltip (`tooltip`, off by default as in Python).
- Pinned apps are `%LOCALAPPDATA%\waku\taskbar_pinned.json` (copied once from `YASB`), read and written
  through `PinStore`.

Options (Python names, camel case): `iconSize`, `tooltip`, `showOnlyVisible`, `strictFiltering`,
`monitorExclusive`, `hideEmpty`, `grouping`, `showCount`, `ignoreTitles`, `ignoreClasses`,
`ignoreProcesses`.

## What is reused and what is new

| Piece | Where (`src/features/taskbar/`) | Layer | Use |
|---|---|---|---|
| `WindowTracker` (shell hook, WinEvent hooks, taskbar-window policy, active and flash state) | `window_tracker.*`, `win32/window_tracker_hooks_win.cpp` | root, `win32/` | one per run, owned by the service |
| `WindowSystem` (query, focus, minimise, close, app id, icon) | `window_system.h`, `win32/window_system_win.cpp` | root, `win32/` | tests substitute `testing/fake_window_system.h` |
| `PinSupport` (pin metadata, launch, pinned icons) | `pin_support.h`, `win32/pin_support_win.cpp` | root, `win32/` | tests substitute a fake |
| `PinStore` (taskbar_pinned.json, change signal) | `pin_store.*` | root (no UI) | as is |
| `TaskbarIcons` (icon loader on a worker thread, cache), `loadPinnedIcon` | `taskbar_icons.*` | root (no UI) | as is |
| `TaskbarService` (singleton) | `qml/taskbar_service.*` | `qml/` (`waku.services`) | tracker + pin store + icons + the focus/close/pin actions |
| `TaskbarModel` (per widget) | `qml/taskbar_model.*` | `qml/` (`waku.services`) | filters and groups the service's windows into button rows |
| `WindowIconProvider` | `qml/taskbar_service.*` | `qml/` (`waku.services`) | `image://windowicon/<key>/<rev>` |
| `Taskbar.qml` | `qml/` | `qml/` (`waku.widgets`) | the widget |

The classes of the Widgets build (`TaskbarWidget`, `AppButton`, `TaskbarMenu`, `PreviewManager`) were
QWidgets and are not used; their logic (filter, group key, `bringToForeground`, pin/unpin) is ported
to the service and the model, not re-invented.

## QML-facing API

`S.TaskbarService` (singleton): `available`, `unavailableReason`; `attach(window)` (starts the
tracker on that bar window's handle, once).

`TaskbarModel` (one per widget, `QML_ELEMENT`): properties `service`, `window`, `grouping`,
`showOnlyVisible`, `strictFiltering`, `monitorExclusive`, `ignoreTitles`, `ignoreClasses`,
`ignoreProcesses`, `count`. Roles: `key`, `title`, `tooltip`, `iconUrl`, `windowCount` (windows in
the button), `pinned`, `running`, `active`, `minimized`, `flashing`, `uniqueId`. Invokables by row:
`activate`, `middleClick`, `closeWindow`, `closeAll`, `togglePin`, `launch`.

Progress roles: `progressState` is `none`, `indeterminate`, `normal`, `error` or `paused`;
`progress` is a fraction from 0 to 1 (ignored for `none` and `indeterminate`). These are also
Windows extensions on `Quickshell.Wayland.Toplevel`. A port chooses its own indicator and colours.
Grouped rows choose error, paused, normal, then indeterminate; ties use the least progress.
Flashing for attention stays separate from progress.

Row order: pinned apps in pin-file order, then the other buttons in the order their first window
appeared.

`S.WindowControl` (singleton, `qml/window_control.h`): `activate(hwnd)`, `foreground()`,
`isWindow(hwnd)`. For a shell that knows HWNDs from elsewhere and has no tracker
rows, because the tracker needs a bar window to attach to. `activate` restores the window when it
is minimised (`ShowWindowAsync(SW_RESTORE)`), then calls `kernel::forceForegroundFocus`; it
returns true when the window is the foreground window afterwards. Windows' foreground lock can
still refuse a window of another process: the window's taskbar button then flashes
(`FlashWindowEx`) and the call returns false. A missing window or `hwnd <= 0` returns false.
Everything sits behind `WindowControlBackend` (`window_control_backend.h`), with a Win32
implementation and a fake in `testing/`; without a live desktop all three calls return false or 0.
No new Windows API. It does not touch the tracker, so it does not toggle or minimise (unlike
`TaskbarModel.activate`).

## Decisions

1. **Service plus per-widget model.** The tracker, pin store and icon cache exist once per run
   (like Python's shared `TaskbarWindowManager`); each widget has its own filter (monitor, strict,
   hidden), so the rows are a model per widget that recomputes from the service on every change
   and applies the difference (remove, insert, move, `dataChanged`), keeping delegates and an open
   menu alive.
2. **The service is not default-constructible** (Qt prefers a singleton's default constructor to
   `create()`, and `create()` registers the image provider). Same seam as `Systray`:
   `setFactoryForTests`; `kernel::liveDesktop()` gates the real tracker.
3. **Icons through an image provider** with the same shape as `image://tray`: the service keeps the
   32 px image of each key and a revision that is bumped when an asynchronous load finishes, so
   QML reloads exactly that icon. A pinned app that does not run uses a service-assigned
   "pseudo" key (high bit set) so it has an icon and no window.
4. **The shell hook window is the bar window.** `WindowTracker::start` needs a top-level handle;
   the first widget passes `Window.window` (as `TrayPins.window`). Hooks are only installed on a
   real platform; on `offscreen` the service says `available: false` and never enumerates real
   windows.
5. **Middle click closes** (Python has no default for it; the brief asked for close or launch).
6. **Pins are written only from the menu.** Reorder by drag is out of scope, as for the tray;
   the order is the file's order.
7. **Progress uses our own tray host only.** A receiver window on its own thread
   (`win32/progress_receiver_win.cpp`) lives for the whole process. Once per second it looks for a
   `Shell_TrayWnd` owned by this process and sets its `TaskbandHWND` property to that receiver.
   It never writes to Explorer or another shell. `ITaskbarList3` sends `WM_USER + 64` (value,
   scaled by 65534) and `WM_USER + 65` (state) there, synchronously, which is why the receiver
   has its own thread: a reload on the GUI thread never blocks the sender. The receiver accepts
   only valid scalar payloads for live windows, allows these two messages through UIPI and never
   forwards commands. It writes a `ProgressCache` keyed by window and owning process, so a reused
   handle never inherits a state. Applications keep the receiver they found, so the receiver and
   the cache outlive a reload: the new tracker reads the cache when it adds a window. A window
   that hides keeps its progress; a destroyed one, or one the receiver finds gone, loses it.
   Debug logging category `waku.taskbar.progress` logs every message and bind. See the
   undocumented-API allowlist.

## Progress limitations

The port must instantiate the tray service, without `--no-tray`, and waku's tray host must own
the shell lookup. With `--keep-taskbar`, Explorer keeps that lookup and progress is unavailable.
This version does not observe Explorer or take over its receiver. An application that cached
Explorer's receiver before waku attached can keep sending there: open a new application window or
restart it after waku starts. No global `TaskbarCreated` or `TaskbarButtonCreated` is sent just to
recover it. Restarted tray hosts are rebound by the timer. An app that never publishes progress
cannot be detected as busy. Terminal progress belongs to the terminal window, aggregated over its
tabs, not to one CLI process or tab; Terminal does not report again when a tab closes or another
tab is selected, so a closed busy tab can leave the window busy until it reports again.

To tell "the application sent nothing" from "waku dropped it", run with `--log-file` and
`QT_LOGGING_RULES=waku.taskbar.progress.debug=true`.

## Left out

- **Hover preview and peek** (`preview.*`): the Python build and the Widgets build draw DWM
  thumbnails into child widgets and reorder a live-preview peek window. A Qt Quick window has no per-item native
  child to register a thumbnail against; this needs its own design (a transparent window per
  thumbnail, with the thumbnail registered against it and cropped to the item). Deferred, not
  faked: the options are not read, and a click on a group cycles instead of opening a preview.
- **Animation** of buttons being added and removed, the title label (`title_label`), drag reorder,
  the Recycle Bin pin, Explorer quick-access and "New window" jump list entries, "End task",
  keybindings and `on_*` callbacks, `ignore_apps` as one nested object (three flat lists
  instead).
- A `--no-taskbar` switch: `src/app/waku/main.cpp` would call
  `kernel::setCapabilityDisabled("taskbar", true)` and the service would ask
  `kernel::liveDesktop("taskbar")`.
- Live check against real windows. Tests run on fakes only.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- `waku.services.WindowControl` (`src/features/taskbar/qml/window_control.{h,cpp}`): `activate(hwnd)` (restore when minimised, then `kernel::forceForegroundFocus`; the taskbar button flashes and the call returns false when the foreground lock refuses), `foreground()` and `isWindow(hwnd)` for a shell that gets HWNDs from elsewhere (komorebi state) and has no tracker rows. A singleton only because QML needs an object to call; it holds no state, a thin call into Win32 through a `WindowControlBackend`. GUI thread only, no thread, no timer. Inert (false, 0) when `kernel::liveDesktop()` is false

- `waku.services.TaskbarService` (`src/features/taskbar/qml/taskbar_service.{h,cpp}`): the one window tracker (`waku::taskbar::WindowTracker`: shell hook and WinEvent hooks), pinned-apps store (`taskbar_pinned.json`) and window-icon cache of a `waku` run, shared by every Taskbar widget; created by the QML engine on first use and destroyed with it (the tracker stops, the icon loader's one-thread pool is joined). Registers `image://windowicon`. The tracker starts when the first Taskbar widget hands over its bar window; it never starts on the offscreen platform.
