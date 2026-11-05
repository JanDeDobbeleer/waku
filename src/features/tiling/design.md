# tiling

A built-in, opt-in tiler. A window that opens goes into the slot of its layout instead of floating
where the app put it. It uses documented Win32 only, runs on the GUI thread, polls nothing and
injects nothing. A shell turns it on; the engine ships no default beyond what any shell needs.

```qml
Component.onCompleted: {
    Tiling.layout = "mainStack"   // see Layouts
    Tiling.gap = 8
    Tiling.ratio = 0.5
    Tiling.rules = [{ process: "Calculator.exe", action: "float" },
                    { class: "TaskManagerWindow", action: "ignore" }]
    Tiling.enabled = true
}
```

## Parts

| File | Role |
|---|---|
| `layouts.*` | pure geometry: `layoutSlots`, `outerRect` (border compensation), names |
| `tiling_model.*` | per `(HMONITOR, desktop GUID)` ordered slots (a slot is one window or a stack of windows), layout override, float set |
| `tiling_rules.*` | user rules (`process`, `class`, `title` regex -> `ignore` or `float`) and `classify`, the eligibility function |
| `window_placer.h` | the interface to the machine: enumerate, query facts, work area, shown desktop, `place`, `raise`, `activate` |
| `tiling_engine.*` | the behaviour: events in, placements out |
| `tiling_events.h` | event kinds, WinEvent codes, the `TilingHooks` interface |
| `tiling_command.*` | the `tile` verb of `wakuc` over `TilingControl` |
| `win32/` | `WindowPlacer` and `TilingHooks` for Windows |
| `qml/tiling_service.*` | the `Tiling` singleton: settings, coexistence guard, screen debounce |
| `testing/fake_window_placer.h` | fake placer and manual clock for the tests |

Tests never touch a real window: the engine sees the machine through `WindowPlacer`, and time
through `Scheduler`.

## Events

One out-of-context range hook per group (`kernel::WinEventHook`): destroy, show and hide; name
change; cloak and uncloak; minimize start and end; move and size start and end; foreground.
Location change is never hooked. The hook filters `idObject == OBJID_WINDOW`, `idChild == 0` and
`GetAncestor(GA_ROOT) == hwnd` (not for destroy: a destroyed window has no ancestor). Events from
waku's own process are skipped by the hook (`WINEVENT_SKIPOWNPROCESS`).

- **A new window.** `Show` arrives before the final title and size. The engine evaluates one
  event-loop turn later; a window that is still minimized or untitled is looked at again every
  100 ms, five times at most. `NameChange` looks again only at a window that is new or was rejected
  for want of a title.
- **A window goes.** `Destroy`, `Hide` of a hidden window, and `MinimizeStart` take it out of its
  stack. `MinimizeEnd` evaluates it again; it returns at the end of the stack.
- **Cloak.** A desktop switch cloaks every window of the old desktop. A cloaked window whose
  desktop is not the shown one keeps its place in that desktop's stack; one that cloaked itself
  on the shown desktop leaves. The decision waits 150 ms because the registry value that names the
  shown desktop can trail the event.
- **Desktop switch.** Windows has no documented notification. The first `Uncloaked`, `Cloaked` or
  `Foreground` after a switch reads `CurrentVirtualDesktop` (`vdesk::currentDesktopId`, the row
  that already is on the allowlist) and retiles the new desktop. No timer.
- **User intent.** `MoveSizeStart` remembers the bounds, `MoveSizeEnd` compares: a window that
  moved or resized by more than 2 px floats. A clamped window counts as unmoved because the
  comparison starts from the bounds it had at the start. A window the user holds is not placed.

## Eligibility

A window is managed unless: not visible; cloaked; minimized or untitled (retry); a window of waku;
owned by a visible window; a tool window; without `WS_THICKFRAME` or `WS_CAPTION`; maximized by the
user; fullscreen; a splash or shell class (`isBuiltinIgnoredClass`); ignored by a rule; or the window
of an elevated process while waku is not elevated (UIPI forbids moving it; unmanaged, no error).
A `float` rule keeps the window, unplaced; `toggle` tiles it. A maximized managed window keeps its
place in the stack but takes no slot.

## Placement

`SetWindowPos(NOZORDER | NOACTIVATE | NOSENDCHANGING | NOCOPYBITS | FRAMECHANGED |
ASYNCWINDOWPOS)` per window; not `DeferWindowPos`, which fails as a batch on one bad window. The
async flag keeps a hung app from blocking the GUI thread, so the readback right after the call can
be stale: 150 ms later the engine reads the bounds again. A window that clamps (a minimum size)
keeps its slot, leaves a gap and is counted in `status`; there is no second pass. The slot is the
visible bounds; `outerRect` adds the invisible border per edge (`GetWindowRect` minus
`DWMWA_EXTENDED_FRAME_BOUNDS`). The work area is `rcWork` of the window's monitor in physical
pixels. A cloaked window is never placed. Retile requests are coalesced: any number in one turn
place each window once.

Screen changes (`screenAdded`, `screenRemoved`, `availableGeometryChanged`, `geometryChanged`)
retile after 400 ms.

## Snap

Snap Layouts and Snap Assist are not suppressed: a window the user snaps by mouse floats (move and
size end).

## Commands

## Layouts

The names are case-insensitive and ignore `-`, `_` and spaces (`main-stack-top` is
`mainStackTop`). `Tiling.layout` is the default; `wakuc tile layout <name>` sets one workspace;
`wakuc tile layout next|previous` walks the list below for the focused window's workspace, wrapping
(`tile status` shows the layout in force). `ratio` (0.5 by default) is the share of the first
split: of the width for `mainStack`, `bsp` and `centerMain`, of the height for `mainStackTop`. One
window fills the area in every layout. `tile flip` mirrors the slots of any of them.

```text
mainStack            mainStackTop         columns              rows
+-----+-----+        +-----------+        +----+-----+----+    +-----------+
|     |  2  |        |     1     |        |    |     |    |    |     1     |
|  1  +-----+        +-----+-----+        | 1  |  2  | 3  |    +-----------+
|     |  3  |        |  2  |  3  |        |    |     |    |    |     2     |
+-----+-----+        +-----+-----+        +----+-----+----+    +-----------+
                                                                |     3     |
                                                                +-----------+
bsp                  grid                 centerMain
+-----+-----+        +-----+-----+        +----+-------+----+
|     |  2  |        |  1  |  2  |        | 3  |       | 2  |
|  1  +--+--+        +-----+-----+        +----+   1   +----+
|     |3 |4 |        |  3  |  4  |        | 5  |       | 4  |
+-----+--+--+        +-----+-----+        +----+-------+----+
```

- `mainStack`: main left, the rest stacked right. The only layout with height weights.
- `mainStackTop`: main on top, the rest side by side below.
- `columns`, `rows`: equal columns or bands; they do not resize.
- `bsp`: window 1 takes `ratio` of the width; every next window takes the top (then left, then
  top...) half of what is left. 
- `grid`: ceil(sqrt(n)) columns filled by row; a short last row spreads over the width. Does not
  resize.
- `centerMain`: main in the middle; the others alternate right, left, right... in two columns.
  With two windows it is `mainStack`.

`wakuc tile ...` acts on the foreground window (`TilingControl`, `tiling_command.h`; the service
answers `ok: ...` or `error: ...`). The pure logic is in `layouts.cpp` (`flipSlots`,
`neighbourSlot`, weights) and `tiling_model.cpp` (`swap`, `promote`, the per-workspace
`WorkspaceView`); the engine ties them together and a fake placer tests them.

- `focus left|right|up|down`: the nearest tiled window of the workspace by slot centre, the
  sideways offset counting double, so "right" of the main window is the stack window at its
  height. No wrap. `focus next|previous` keep the stack order.
- `move <direction>`: swaps the focused window with that neighbour in the stack order and
  retiles. `promote`: the focused window takes slot 0 (unchanged if it already has it).
- `resize horizontal`: changes the `ratio` of the layout by 5% per call (a stack window
  that grows takes width from the main window), clamped to 0.1..0.9, per workspace. `vertical`: a
  height weight (step 0.1, 0.3..3) for a stacked window of `mainStack`, so it needs three or more
  windows. `columns`, `rows` and `grid` do not resize. A change of `Tiling.ratio` clears the overrides.
- `flip horizontal|vertical`: mirrors the slots of the layout in the work area; again undoes it.
- `monocle`: the focused window gets the whole work area (inside the gap); the other windows are
  not touched, so they stay behind it. Again re-tiles. A monocle window that leaves the
  stack ends the mode.
- `pause`: no window is placed while paused (windows are still tracked, a drag does not float
  one); resuming retiles. Not the same as `Tiling.enabled = false`, which drops every hook.
- `close`, `minimize`: any visible foreground window, tiled or not, except windows of waku and the
  desktop and taskbar classes. `close` posts `WM_CLOSE` (a window may ask to save), `minimize`
  uses `ShowWindowAsync`; neither blocks on a hung window.

## Stacking

Several windows can share one slot of the layout. A workspace is an
ordered list of slots (`Slot` in `tiling_model.h`); a slot holds an ordered list of windows and
the index of the top one. A workspace with no stacks is a list of one-window slots, so it behaves
as before. The layout counts slots, not windows.

- `stack left|right|up|down`: the focused window leaves its slot and joins the stack of the
  slot nearest in that direction (the same geometry as `focus`). It becomes the top there. The
  slot it left is dropped when it was alone; else the window under it shows.
- `unstack`: the focused window leaves its stack and gets a slot of its own right after the
  stack's slot. A stack of one dissolves by itself.
- `cycle-stack next|previous`: the next or previous window of the focused window's stack (wrapping)
  becomes the top and takes the focus.
- Only the top window is a layout slot. The windows under it are placed at the same rectangle
  (`place`, no activation) and stay managed: they are not minimized or hidden. A window that
  becomes the top of a stack with two windows or more is raised with `SetWindowPos(HWND_TOP,
  SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE)` (`WindowPlacer::raise`); nothing else is raised, so
  floating windows keep their order.
- A window under the top that takes the foreground (alt-tab, the taskbar) becomes the top.
- A window that closes, hides, minimizes, floats, or leaves the desktop leaves its stack. When it
  was the top, the next window down the list shows. The height weight and the monocle mark
  follow the top of a slot.
- `focus left|right|up|down`, `move`, `promote`, `resize`, `flip` and `monocle` act on slots: focus
  goes to the top of the neighbouring slot, `move` and `promote` move the whole stack, `resize` and
  `monocle` size the slot. `focus next|previous` visit slots.
- `status` prints `stacks=N`, the number of slots with more than one window.

## Decisions

- State is in memory only. A reload of the shell builds a new service, which scans the open
  windows again; floats chosen with `toggle` are forgotten.
- One layout name per workspace, set by `wakuc tile layout`; `Tiling.layout` is the default for
  workspaces without one and clears the overrides when it changes.
- New windows go to the end of the stack; the first window is the main one.
- No undocumented API. The shown desktop reuses the registry value on the allowlist.

## Open points

- Keyboard Snap (Win+Arrow) and a maximize emit no event the tiler hooks, so a snapped window is
  noticed at the next retile; a window maximized or restored by the user takes or frees its slot
  only then. `retile` fixes it by hand.
- No per-monitor gap and no persistence. Flips, ratio, weights and monocle live in
  memory per workspace.
- Exclusive-fullscreen games are detected only by their bounds.
- UWP frames (`ApplicationFrameWindow`) that animate their size can clamp for a moment.
