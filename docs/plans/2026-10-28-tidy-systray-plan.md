# tidy systray: plan

Status: decisions taken, nothing built. Sits on `spike/qt-quick-bar` after popups, tooltips, safe
mode and `tidybarc`. Background: `2026-10-28-qt-quick-bar-config-design.md` (the `tidy` modules,
`Pill`, `PopupWindow`, `Tooltip`, `Theme`) and the Widgets build's systray (`widgets/systray/`,
`platform/systray_*`, `hook/`).

## What the widget does

Shows the notification-area icons of other programs in the bar. Icons can be pinned to the bar or
kept in an "unpinned" group (inline, or in a popup grid behind a chevron button). A click reaches
the owning program as the mouse messages it expects, hovering shows the program's tooltip, pin
state and order persist per screen, and the icons come back when Explorer restarts.

Not in this plan: balloon notifications (`szInfo`), the Windows 11 hidden-icon flyout, the system
icons Windows draws itself (battery, volume, network; tidy has widgets for them), and dragging
(see decision 4).

## What already exists and what that means

The hard, risky part is done and was run live in the Widgets build (parity checklist: "done
2026-10-28"). Tidy reuses it instead of porting it again.

| Piece | Where | Layer | Reuse |
|---|---|---|---|
| `TrayHost` (signals `iconModified`, `iconDeleted`, `updateIcons`) and `SystrayHook` (a hook DLL in Explorer's `Shell_TrayWnd` thread, messages over a pipe) | `platform/systray_*` | platform | as is |
| `TrayWindowApi` (is the window alive, allow foreground, send a notify message, cursor, `TaskbarCreated` broadcast) | `platform/systray_host.h` | platform | as is |
| `TrayIconData` (raw RGBA icon, tip, guid, hwnd, uID, callback message, version) and the `SHELLTRAYDATA` parser | `platform/systray_types.h`, `systray_pipe.*` | platform | as is |
| `SystrayService` / `createTrayHost` (one host per run) | `widgets/systray/systray_service.*` | widgets (no UI) | as is, without the legacy factory |
| `IconStateMap`, `updateCurrentState`, `updateIconData`, `iconStateKey`, `SystrayStateStore` (per-screen JSON), `cleanScreenId` | `widgets/systray/systray_state.*` | widgets (no UI) | as is, except `appDataDir()` hardcodes `YASB` |
| `scaleTrayIcon` (Lanczos to 32 px plus sharpen) | `widgets/systray/icon_scale.*` | widgets (no UI) | as is |
| The hook DLL (`YASBNativeTrayHook[_arm64].dll`) | `hook/trayhook.cpp` | native | the same DLL and the same pipe and mutex names, copied next to `tidybar.exe` |

Logic that is tangled into Widgets classes and must be **extracted** (then used by both builds):

- **Click to messages** (`TrayIconButton::sendAction` / `sendNotify`): which Win32 messages a click
  produces, `allowSetForeground` first, `NIN_SELECT` and `NIN_CONTEXTMENU` for version 3 and up,
  and the version-4 packing of the cursor position into `wParam` and of the event and id into
  `lParam`. Becomes a pure function in `platform/` plus tests with a fake `TrayWindowApi`.
- **Identity and filtering** (`findIcon`, `isHiddenIcon`, the filtered guids, `checkIcons` dropping
  icons whose window is gone, the saved-state lookup by guid then exe path). Becomes a UI-free
  `TrayIconRegistry` (add, modify, delete, stale sweep) with tests; the QML model sits on top.

## Decisions

1. **Pin state file: separate.** `%LOCALAPPDATA%\tidy\systray_state_<screen>.json`, same JSON
   format. `appDataDir()` gets a parameter.
2. **One hook at a time, so no second DLL.** `yasb-native` is shut down while `tidybar.exe` runs.
   Tidy reuses the existing hook DLL and its pipe and mutex names, and the build copies the DLL
   next to `tidybar.exe`. The Widgets build's CMake target stays the DLL's owner until that build
   is retired; then `hook/` moves and is renamed. Safety net: if the pipe or mutex is already held
   (the Widgets build is still running), `SystrayHook::valid()` is false and the widget says "tray
   unavailable: another tray host is running" instead of doing anything to Explorer.
3. **No legacy mode.** The fallback that replaces `Shell_TrayWnd` with our own window takes over
   the tray for the whole session and has no automated test. When the hook cannot be used the
   widget reports "tray unavailable" and shows nothing.
4. **No dragging; popups and a small menu.** See the answer below.
5. **System icons** (battery, volume, network GUIDs): hidden by default, option to show them.
6. **Live testing.** The hook injects a DLL into `explorer.exe`. Everything before the live phase
   runs on fakes and the offscreen platform and never touches Explorer. The live phase needs the
   Widgets build stopped first and your explicit go.

### Do we need drag? No.

Drag in the Widgets build does three things: move an icon between the bar and the unpinned popup
(pin and unpin), reorder icons inside a group, and show a drop marker. All three have a plainer
replacement that needs only the popups we already have:

- **Pin and unpin:** a modifier click (Ctrl by default, the Widgets build's setting), or "Pin to
  bar" / "Unpin" in the icon's small menu.
- **Order:** a new pin goes to the end; "Move left" and "Move right" in the same menu change it.
  The order is saved per screen, as now.
- **The menu** is a `PopupWindow` opened by a right click with a modifier (or by a click on the
  chevron's context entry); a plain right click still goes to the program, as it must.

What drag would cost: a system drag across two of our windows (the bar and the popup) has not been
shown to work in Qt Quick, so it would start with a spike and might end with this fallback anyway.
It can come later without changing the model: `TrayPins.move(key, pinned, toIndex)` is the same call
a drop would make.

## Design

### Pieces (QML-facing)

```
tidy.services
  Systray            singleton. Owns the shared TrayHost through SystrayService, applies
                     TrayIconRegistry, exposes `available`, `unavailableReason`, `icons`.
  TrayIconModel      QAbstractListModel behind Systray.icons. Roles: key, tooltip, exe, iconUrl,
                     hwnd, hasGuid. dataChanged on update, rows removed on delete.
                     Invokables on Systray: click(id, button), doubleClick(id), refresh().
  TrayPins           instantiable, one per Tray widget. Takes the model and a screenId; exposes
                     `pinned` and `unpinned` models in saved order, setPinned(key, bool),
                     move(key, pinned, toIndex); loads and saves the per-screen state file.
  image provider     image://tray/<key>/<revision>: the RGBA bytes through scaleTrayIcon, no
                     encoding. Registered by the module (QQmlEngineExtensionPlugin), so any
                     engine that imports tidy.services has it.
tidy.widgets
  Tray               the widget: pinned icons, the unpinned group, the chevron button.
  TrayIconButton     one icon: image, tooltip, click handling, modifier-click to pin, its menu.
```

The raw RGBA icon cannot go through a `data:` URL the way album art does (it is not an encoded
image, and tray icons can change every second), hence the image provider. The revision in the URL
makes the `Image` reload when the icon changes.

### Options of the `Tray` widget

`iconSize` (16), `hide` (list of exe names), `showUnpinned` (true), `unpinnedInPopup` (false),
`iconsPerRow` (5), `showSystemIcons` (false), `pinModifier` (Ctrl), `tooltip` (true). Collapsed and
expanded button glyphs are properties of the chevron `Pill`. These are the Widgets build's options
minus the CSS ones and `use_hook` (there is only the hook).

### Threads and lifetime

Unchanged from the Widgets build: the host's worker thread parses and emits queued signals; the
registry, the model and the widgets live on the GUI thread. The `Systray` singleton stops the host
(releasing the pipe and mutex; the DLL detaches itself within 5 s) when the engine destroys it. The
control channel's `quit` and a normal exit both go through that path. No new thread.

### Clicks

`Systray.click(id, button)` runs the extracted click function with the real `TrayWindowApi`. Press
and release both go to the program (`WM_*BUTTONDOWN`, then `WM_*BUTTONUP`), a double click sends
`WM_LBUTTONDBLCLK` and swallows the next release, and a press-and-release more than a few pixels
apart is not a click. The QML button decides press, release and distance; the function only builds
messages.

## Phases

Each phase ends with passing tests and a commit; sizes are my estimate of effort.

| # | Phase | Delivers | Touches Explorer | Size |
|---|---|---|---|---|
| 1 | Extract and test | **Done** (`d74eff5`, `93eff1f`). `platform/systray_click` (`sendTrayAction`, `sendTrayNotify`), `TrayIconRegistry` (identity, filter, stale sweep), `savedStateFor`, `appDataDir(folder)`. The Widgets build uses them; its 49 systray tests and the full suite (832) pass unchanged; 42 new tests. | no | M |
| 2 | `Systray` and the model | **Done.** `Systray` singleton (`click` and `doubleClick` take the icon `id`, since exe-path keys can repeat), `TrayIconModel`, `TrayImageProvider`; 19 new tests on a fake host and window API. Planned: singleton, `TrayIconModel`, the image provider (with a changing-icon test), "unavailable" states; tests with a fake `TrayHost` and `TrayWindowApi` (add, update, delete, filter, stale, click messages). | no | M |
| 3 | `TrayPins` | **Done.** `TrayPins` (instantiable, one per Tray widget; `setPinned`, `move`, `moveBy` and `isPinned` take the icon `id`) with `TrayPinList` models for `pinned` and `unpinned`, saved order and per-screen state file in the Widgets build's format. Planned: pinned and unpinned models, order, move, per-screen persistence; tests with a temp state dir and the existing state vectors. | no | S |
| 4 | `Tray` widget | pinned row, unpinned inline and popup, chevron, tooltips, click, modifier pin, the icon menu (pin, unpin, move, refresh); offscreen tests with the fake host injected through a test seam. | no | L |
| 5 | Live bring-up | the hook DLL copied next to `tidybar.exe`; `--no-tray` switch; run against the real Explorer with the Widgets build stopped: icons appear, left, right, middle and double click, tooltip, pin, order, Explorer restart, quit leaves the shell tray intact, a forced kill lets the DLL detach. | **yes** | M |
| 6 | Polish | icon sizes at other DPIs, hide lists, system icons option, docs, notes for retiring the Widgets build's copy. | yes | S |

## Tests

- Phases 1 to 4 run on fakes (`TrayHost`, `TrayWindowApi`) and the offscreen platform; the service
  is not created offscreen (as in the Widgets build), so a test can never touch the real tray.
- The click function and the registry get table-driven tests that also run against the existing
  Widgets vectors where they apply (`tests/widgets/test_systray*.cpp`).
- Live-only (phase 5, by hand): the list above. The hook has no automated test in the Widgets
  build either, and this plan does not change that.

## Risks

| Risk | Mitigation |
|---|---|
| Injecting into Explorer disrupts the user's session | phases 1 to 4 never touch it; phase 5 only with your go and the Widgets build stopped; `--no-tray`; the hook is the one already run daily by the Widgets build; the DLL detaches within 5 s when the host's mutex goes |
| `tidybar.exe` starts while the Widgets build still holds the pipe and mutex | `valid()` is false, the widget reports it and does nothing to Explorer |
| Tray icons update very often (progress, media) | model `dataChanged` on one row, image revision bump only when the pixels changed |
| An icon with a version-4 callback behaves differently | the click function is the single place that encodes it, with tests per version |
| The extraction breaks the Widgets build | its tests stay in the same run; phase 1 ends with both builds green |
| Undocumented Windows API use grows | none added: all of it is already in the allowlist of `docs/architecture.md`, and tidy adds no call |

## Next step

Phase 4 (`Tray` widget). Offscreen tests with the fake host injected; nothing touches Explorer.
