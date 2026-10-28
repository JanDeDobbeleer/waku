# tidy systray: plan

Status: proposal, nothing built. Sits on `spike/qt-quick-bar` after popups, tooltips, safe mode and
`tidyc`. Background: `2026-10-28-qt-quick-bar-config-design.md` (the `tidy` modules, `Pill`,
`PopupWindow`, `Tooltip`, `Theme`) and the Widgets build's systray (`widgets/systray/`,
`platform/systray_*`, `hook/`).

## What the widget does

Shows the notification-area icons of other programs in the bar. Icons can be pinned to the bar or
kept in an "unpinned" group (inline, or in a popup grid behind a chevron button). A click reaches
the owning program as the mouse messages it expects, hovering shows the program's tooltip, pin
state and order persist per screen, and the icons come back when Explorer restarts.

Not in this plan: balloon notifications (`szInfo`), the Windows 11 hidden-icon flyout, and the
system icons Windows draws itself (battery, volume, network), which tidy has widgets for.

## What already exists and what that means

The hard, risky part is done and was run live in the Widgets build (parity checklist: "done
2026-10-28"). Tidy reuses it instead of porting it again.

| Piece | Where | Layer | Reuse |
|---|---|---|---|
| `TrayHost` (signals `iconModified`, `iconDeleted`, `updateIcons`), `SystrayHook` (a hook DLL in Explorer's `Shell_TrayWnd` thread, messages over a pipe) and `SystrayLegacy` (our own `Shell_TrayWnd` window) | `platform/systray_*` | platform | as is |
| `TrayWindowApi` (is the window alive, allow foreground, send a notify message, cursor, `TaskbarCreated` broadcast) | `platform/systray_host.h` | platform | as is |
| `TrayIconData` (raw RGBA icon, tip, guid, hwnd, uID, callback message, version) and the `SHELLTRAYDATA` parser | `platform/systray_types.h`, `systray_pipe.*` | platform | as is |
| `SystrayService` / `createTrayHost` (hook if usable, else legacy; one host per run) | `widgets/systray/systray_service.*` | widgets (no UI) | as is |
| `IconStateMap`, `updateCurrentState`, `updateIconData`, `iconStateKey`, `SystrayStateStore` (per-screen JSON), `cleanScreenId` | `widgets/systray/systray_state.*` | widgets (no UI) | as is, except `appDataDir()` hardcodes `YASB` |
| `scaleTrayIcon` (Lanczos to 32 px plus sharpen) | `widgets/systray/icon_scale.*` | widgets (no UI) | as is |
| The hook DLL source | `hook/trayhook.cpp` | native | rebuilt under tidy's own names (below) |

Logic that is tangled into Widgets classes and must be **extracted** (then used by both builds):

- **Click to messages** (`TrayIconButton::sendAction` / `sendNotify`): which Win32 messages a click
  produces, `allowSetForeground` first, `NIN_SELECT` and `NIN_CONTEXTMENU` for version 3 and up,
  and the version-4 packing of the cursor position into `wParam` and of the event and id into
  `lParam`. Becomes a pure function in `platform/` plus tests with a fake `TrayWindowApi`.
- **Identity and filtering** (`findIcon`, `isHiddenIcon`, the filtered guids, `checkIcons` dropping
  icons whose window is gone, the saved-state lookup by guid then exe path). Becomes a UI-free
  `TrayIconRegistry` (add, modify, delete, stale sweep) with tests; the QML model sits on top.

## Decisions to make (my recommendation first)

1. **Pin state file.** Separate (`%LOCALAPPDATA%\tidy\systray_state_<screen>.json`, same format) or
   shared with the Widgets/Python build? Recommend separate: tidy is its own app and a shared file
   would let two apps rewrite each other's order. Needs `appDataDir()` parameterised.
2. **Two tray hosts at once.** The Widgets build is running on this machine now. Tidy's hook DLL,
   pipe and mutex must have their own names (`TidyTrayHook[_arm64].dll`, `tidy_systray_monitor`,
   `TidyTrayHookAlive`), built from the same `trayhook.cpp` with different compile definitions.
   The architecture doc says two hooks coexist (each subclasses `Shell_TrayWnd` and chains to the
   previous procedure); that has not been tested with two of ours. Recommend: own names, and test
   coexistence explicitly in phase 5.
3. **Legacy mode.** The fallback that replaces `Shell_TrayWnd` with our own window takes over the
   tray for the whole session and is not covered by tests. Recommend leaving it out of v1: when the
   hook cannot be used the widget reports "tray unavailable" and shows nothing.
4. **Drag and drop between the bar and the popup.** The Widgets build uses `QDrag`. Qt Quick can
   start a system drag (`Drag.dragType: Drag.Automatic`) and receive it in a `DropArea` in another
   window, but I have not seen it work across two of our windows. Recommend a small spike first
   (phase 0); if it fails, pinning and unpinning is a modifier click or a menu entry and order
   changes by "move left / right".
5. **System icons** (battery, volume, network GUIDs): hidden by default, option to show them.
6. **Live testing.** The hook injects a DLL into `explorer.exe`. Everything before phase 5 runs on
   fakes and the offscreen platform and never touches Explorer. Phase 5 needs your explicit go, and
   I need to know whether to stop the Widgets build during it.

## Design

### Pieces (QML-facing)

```
tidy.services
  Systray            singleton. Owns the shared TrayHost through SystrayService, applies
                     TrayIconRegistry, exposes `available`, `mode` ("hook"), `icons`.
  TrayIconModel      QAbstractListModel behind Systray.icons. Roles: key, tooltip, exe, iconUrl,
                     hwnd, hasGuid. dataChanged on update, rows removed on delete.
                     Invokables on Systray: click(key, button), doubleClick(key), refresh().
  TrayPins           instantiable, one per Tray widget. Takes the model and a screenId; exposes
                     `pinned` and `unpinned` models in saved order, setPinned(key, bool),
                     move(key, pinned, toIndex); loads and saves the per-screen state file.
  image provider     image://tray/<key>/<revision>: the RGBA bytes through scaleTrayIcon, no
                     encoding. Registered by the module (QQmlEngineExtensionPlugin), so any
                     engine that imports tidy.services has it.
tidy.widgets
  Tray               the widget: pinned icons, the unpinned group, the chevron button.
  TrayIconButton     one icon: image, tooltip, click handling, modifier-click to pin.
```

The raw RGBA icon cannot go through a `data:` URL the way album art does (it is not an encoded
image, and tray icons can change every second), hence the image provider. The revision in the URL
makes the `Image` reload when the icon changes.

### Options of the `Tray` widget

`iconSize` (16), `hide` (list of exe names), `showUnpinned` (true), `unpinnedInPopup` (false),
`iconsPerRow` (5), `showSystemIcons` (false), `pinModifier` (Ctrl), `tooltip` (true). Collapsed and
expanded button glyphs are properties of the chevron `Pill`. These are the Widgets build's options
minus the CSS ones.

### Threads and lifetime

Unchanged from the Widgets build: the host's worker thread parses and emits queued signals; the
registry, the model and the widgets live on the GUI thread. The `Systray` singleton stops the host
(releasing the pipe and mutex; the DLL detaches itself within 5 s) when the engine destroys it. The
control channel's `quit` and a normal exit both go through that path. No new thread.

### Clicks

The model's `click(key, button)` runs the extracted click function with the real `TrayWindowApi`.
Press and release both go to the program (`WM_*BUTTONDOWN`, then `WM_*BUTTONUP`), a double click
sends `WM_LBUTTONDBLCLK` and swallows the next release, and a drag longer than a few pixels is not
a click. The QML button decides press, release and distance; the function only builds messages.

## Phases

Each phase ends with passing tests and a commit; sizes are my estimate of effort.

| # | Phase | Delivers | Touches Explorer | Size |
|---|---|---|---|---|
| 0 | Spikes | (a) cross-window `Drag.Automatic` between a bar and a popup; (b) an image provider showing a changing RGBA icon. Decides decision 4. | no | S |
| 1 | Extract and test | `platform/systray_click` (pure message function), `TrayIconRegistry` (identity, filter, stale sweep), `appDataDir` parameter. The Widgets build switches to them; its tests stay green. | no | M |
| 2 | `Systray` and the model | singleton, `TrayIconModel`, image provider; tests with a fake `TrayHost` and `TrayWindowApi` (add, update, delete, reorder, filter, stale, click messages). | no | M |
| 3 | `TrayPins` | pinned and unpinned models, order, move, per-screen persistence; tests with a temp state dir and the existing state vectors. | no | S |
| 4 | `Tray` widget | pinned row, unpinned inline and popup, chevron, tooltips, click, modifier pin, context menu entry "Refresh"; offscreen tests with the fake host injected through a test seam. | no | L |
| 5 | Live bring-up | tidy's own hook DLL, pipe and mutex; `--no-tray` switch; run against the real Explorer; check coexistence with the Widgets build, an Explorer restart, quit and kill (the DLL must detach). | **yes** | M |
| 6 | Polish | icon sizes at other DPIs, hide lists, system icons option, docs, parity notes. | yes | S |

## Tests

- Phases 1 to 4 run on fakes (`TrayHost`, `TrayWindowApi`) and the offscreen platform; the service
  is not created offscreen (as in the Widgets build), so a test can never touch the real tray.
- The click function and the registry get table-driven tests that also run against the existing
  Widgets vectors where they apply (`tests/widgets/test_systray*.cpp`).
- Live-only (phase 5, by hand): a real icon appears, left, right, middle and double click reach the
  program, the tooltip matches, Explorer restart brings icons back, quitting tidy leaves the
  shell tray intact. The hook and the legacy window have no automated test in the Widgets build
  either, and this plan does not change that.

## Risks

| Risk | Mitigation |
|---|---|
| Injecting into Explorer disrupts the user's session | phases 0 to 4 never touch it; phase 5 only with your go; `--no-tray`; the hook is the one already run daily by the Widgets build; the DLL detaches within 5 s when the host's mutex goes |
| Two hooks (Widgets build and tidy) interfere | distinct names; explicit coexistence test; if it fails, tidy refuses to start its host while the other is detected and says so |
| Qt Quick cannot drag between windows | phase 0 spike, with the menu fallback in decision 4 |
| Tray icons update very often (progress, media) | model `dataChanged` on one row, image revision bump only when the pixels changed |
| An icon with a version-4 callback behaves differently | the click function is the single place that encodes it, with tests per version |
| Undocumented Windows API use grows | none added: all of it is already in the allowlist of `docs/architecture.md`; the tidy DLL repeats the same two entries under its own names |

## Questions for you

1. Decision 1 to 6 above: do you agree with the recommendations, or want a different call on any?
2. Is the Widgets build (`yasb-native`) allowed to keep running during phase 5, or should I ask you
   to stop it first?
3. Should phase 0 (the two spikes) start right away?
