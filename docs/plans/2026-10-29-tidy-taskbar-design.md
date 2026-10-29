# tidy taskbar: design note

Status: decisions taken, built in this pass. Sits on `spike/qt-quick-bar` next to the Tray widget
(`2026-10-28-tidy-systray-plan.md`). Reference: Python YASB `yasb/taskbar*` (branch `local/build`)
and the Widgets build's `widgets/taskbar/`.

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
- Pinned apps are `%LOCALAPPDATA%\YASB\taskbar_pinned.json`, read and written through the Widgets
  build's `PinStore`, so the two builds share one list.

Options (Python names, camel case): `iconSize`, `tooltip`, `showOnlyVisible`, `strictFiltering`,
`monitorExclusive`, `hideEmpty`, `grouping`, `showCount`, `ignoreTitles`, `ignoreClasses`,
`ignoreProcesses`.

## What is reused and what is new

| Piece | Where | Layer | Use |
|---|---|---|---|
| `WindowTracker` (shell hook, WinEvent hooks, taskbar-window policy, active and flash state) | `platform/window_tracker.*` | platform | as is, one per run, owned by the service |
| `WindowSystem` (query, focus, minimise, close, app id, icon) | `platform/window_system*.h` | platform | as is; tests substitute a fake |
| `PinSupport` (pin metadata, launch, pinned icons) | `platform/pin_support.*` | platform | as is; tests substitute a fake |
| `PinStore` (taskbar_pinned.json, change signal) | `widgets/taskbar/pin_store.*` | widgets (no UI) | as is |
| `TaskbarIcons` (icon loader on a worker thread, cache), `loadPinnedIcon` | `widgets/taskbar/taskbar_icons.*` | widgets (no UI) | as is |
| `TaskbarService` (singleton) | `spike/services/taskbar_service.*` | tidy.services | new: tracker + pin store + icons + the focus/close/pin actions |
| `TaskbarModel` (per widget) | `spike/services/taskbar_model.*` | tidy.services | new: filters and groups the service's windows into button rows |
| `WindowIconProvider` | `spike/services/taskbar_service.*` | tidy.services | new: `image://windowicon/<key>/<rev>` |
| `Taskbar.qml` | `spike/widgets/` | tidy.widgets | new |

The Widgets classes `TaskbarWidget`, `AppButton`, `TaskbarMenu` and `PreviewManager` are QWidgets
and are not used; their logic (filter, group key, `bringToForeground`, pin/unpin) is ported to
the service and the model, not re-invented.

## QML-facing API

`S.TaskbarService` (singleton): `available`, `unavailableReason`; `attach(window)` (starts the
tracker on that bar window's handle, once).

`TaskbarModel` (one per widget, `QML_ELEMENT`): properties `service`, `window`, `grouping`,
`showOnlyVisible`, `strictFiltering`, `monitorExclusive`, `ignoreTitles`, `ignoreClasses`,
`ignoreProcesses`, `count`. Roles: `key`, `title`, `tooltip`, `iconUrl`, `windowCount` (windows in
the button), `pinned`, `running`, `active`, `minimized`, `flashing`, `uniqueId`. Invokables by row:
`activate`, `middleClick`, `closeWindow`, `closeAll`, `togglePin`, `launch`.

Row order: pinned apps in pin-file order, then the other buttons in the order their first window
appeared.

## Decisions

1. **Service plus per-widget model.** The tracker, pin store and icon cache exist once per run
   (like Python's shared `TaskbarWindowManager`); each widget has its own filter (monitor, strict,
   hidden), so the rows are a model per widget that recomputes from the service on every change
   and applies the difference (remove, insert, move, `dataChanged`), keeping delegates and an open
   menu alive.
2. **The service is not default-constructible** (Qt prefers a singleton's default constructor to
   `create()`, and `create()` registers the image provider). Same seam as `Systray`:
   `setFactoryForTests`, `setDisabled`.
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
   the order is the file's order and the Widgets build can still reorder it.
7. **No new Windows API.** Everything goes through `platform/`; nothing is added to the
   undocumented-API allowlist.

## Left out in this pass

- **Hover preview and peek** (`preview.*`): the Widgets build draws DWM thumbnails into child
  QWidgets and reorders a live-preview peek window. A Qt Quick window has no per-item native
  child to register a thumbnail against; this needs its own design (a transparent window per
  thumbnail, with the thumbnail registered against it and cropped to the item). Deferred, not
  faked: the options are accepted by the config layer but have no effect, and a click on a group
  cycles instead of opening a preview.
- **Animation** of buttons being added and removed, the title label (`title_label`), drag reorder,
  the Recycle Bin pin, Explorer quick-access and "New window" jump list entries, "End task",
  keybindings and `on_*` callbacks, `ignore_apps` as one nested object (three flat lists
  instead).
- A `--no-taskbar` switch: `TaskbarService::setDisabled(bool)` exists; wiring it to the command
  line is `spike/main.cpp`'s job.
- Live check against real windows (the orchestrator's live phase). Tests run on fakes only.
