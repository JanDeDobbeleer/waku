# tiling

A built-in, opt-in tiler. A window that opens goes into the slot of its layout instead of floating
where the app put it. It uses documented Win32 only, runs on the GUI thread, polls nothing and
injects nothing. A shell turns it on; the engine ships no default beyond what any shell needs.

```qml
Component.onCompleted: {
    Tiling.layout = "mainStack"   // or "columns"
    Tiling.gap = 8
    Tiling.ratio = 0.55
    Tiling.rules = [{ process: "Calculator.exe", action: "float" },
                    { class: "TaskManagerWindow", action: "ignore" }]
    Tiling.enabled = true
}
```

## Parts

| File | Role |
|---|---|
| `layouts.*` | pure geometry: `layoutSlots`, `outerRect` (border compensation), names |
| `tiling_model.*` | per `(HMONITOR, desktop GUID)` ordered stack, layout override, float set |
| `tiling_rules.*` | user rules (`process`, `class`, `title` regex -> `ignore` or `float`) and `classify`, the eligibility function |
| `window_placer.h` | the interface to the machine: enumerate, query facts, work area, shown desktop, `place`, `activate` |
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

Screen changes (`screenAdded`, `screenRemoved`, `availableGeometryChanged`, `geometryChanged`, the
same Qt signals `KomorebiWorkAreaGuard` uses) retile after 400 ms.

## Coexistence

`Tiling.active` is false, and no hook is installed, while `komorebi.exe` runs (the backend function
of `workspaces/komorebi.h`) or the Komorebi or GlazeWM singleton is connected. The service finds
the two singletons by name on the first `enabled = true` and re-evaluates when either changes, so
it starts by itself when the other tiler quits and stops when it starts. Snap Layouts and Snap
Assist are not suppressed: a window the user snaps by mouse floats (move and size end), which is
the coexistence rule for them.

## Decisions

- State is in memory only. A reload of the shell builds a new service, which scans the open
  windows again; floats chosen with `toggle` are forgotten.
- One layout name per workspace, set by `wakuc tile layout`; `Tiling.layout` is the default for
  workspaces without one and clears the overrides when it changes.
- New windows go to the end of the stack; the first window is the main one.
- No undocumented API. The shown desktop reuses the registry value on the allowlist.
- komorebi (Komorebi License 2.0) and GlazeWM (GPL-3) were read for behaviour only; nothing was
  copied.

## Open points

- Keyboard Snap (Win+Arrow) and a maximize emit no event the tiler hooks, so a snapped window is
  noticed at the next retile; a window maximized or restored by the user takes or frees its slot
  only then. `retile` fixes it by hand.
- No stack reordering (swap, promote to main), no per-monitor gap and no persistence.
- Exclusive-fullscreen games are detected only by their bounds.
- UWP frames (`ApplicationFrameWindow`) that animate their size can clamp for a moment.
