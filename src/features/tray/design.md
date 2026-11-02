# waku systray: plan

Status: built and run live (phase 5 below). Background: `docs/plans/2026-10-28-qt-quick-bar-config-design.md`
(the `waku` modules, `Pill`, `PopupWindow`, `Tooltip`, `Theme`). The systray of the retired Widgets
build (git tag `widgets-main-final`) is where the code came from; see "History".

## What the widget does

Shows the notification-area icons of other programs in the bar. Icons can be pinned to the bar or
kept in an "unpinned" group (inline, or in a popup grid behind a chevron button). A click reaches
the owning program as the mouse messages it expects, hovering shows the program's tooltip, pin
state and order persist per screen, and the icons come back when Explorer restarts.

Not in this plan: balloon notifications (`szInfo`), the Windows 11 hidden-icon flyout, the system
icons Windows draws itself (battery, volume, network; waku has widgets for them), and dragging
(see decision 4).

## What already exists and what that means

The hard, risky part was done and run live in the Widgets build. Waku reuses it instead of porting
it again. Paths are under `src/features/tray/`.

| Piece | Where | Layer | Reuse |
|---|---|---|---|
| `TrayHost` (signals `iconModified`, `iconDeleted`, `updateIcons`) and `SystrayHook` (a hook DLL in Explorer's `Shell_TrayWnd` thread, messages over a pipe) | `tray_host.h`, `win32/tray_hook.*` | root, `win32/` | as is |
| `TrayWindowApi` (is the window alive, allow foreground, send a notify message, cursor, `TaskbarCreated` broadcast) | `tray_host.h` | root | as is |
| `TrayIconData` (raw RGBA icon, tip, guid, hwnd, uID, callback message, version) and the `SHELLTRAYDATA` parser | `tray_types.h`, `tray_pipe.*` | root | as is |
| `SystrayService` / `createTrayHost` (one host per run) | `tray_service.*` | root (no UI) | as is, without the legacy factory |
| `IconStateMap`, `updateCurrentState`, `updateIconData`, `iconStateKey`, `SystrayStateStore` (per-screen JSON), `cleanScreenId` | `tray_state.*` | root (no UI) | as is; the folder is `kernel::appDataDir()` |
| `scaleTrayIcon` (Lanczos to 32 px plus sharpen) | `icon_scale.*` | root (no UI) | as is |
| `colorIconToRgba`, `monochromeIconToRgba`, `trayIconPixelsProblem` (the host-side `GetDIBits` conversion and the blank-icon check) | `icon_bits.*` | root (no UI) | new |
| The hook DLL (`WakuTrayHook[_arm64].dll`) | `hook/trayhook.cpp` | native | copied next to `waku.exe` |

Logic that was tangled into Widgets classes and was **extracted**:

- **Click to messages** (`TrayIconButton::sendAction` / `sendNotify`): which Win32 messages a click
  produces, `allowSetForeground` first, `NIN_SELECT` and `NIN_CONTEXTMENU` for version 3 and up,
  and the version-4 packing of the cursor position into `wParam` and of the event and id into
  `lParam`. It is a pure function (`tray_click.*`) with tests on a fake `TrayWindowApi`.
- **Identity and filtering** (`findIcon`, `isHiddenIcon`, the filtered guids, `checkIcons` dropping
  icons whose window is gone, the saved-state lookup by guid then exe path). It is a UI-free
  `TrayIconRegistry` (`tray_icon_registry.*`: add, modify, delete, stale sweep) with tests; the QML
  model sits on top.

## Decisions

1. **Pin state file: separate.** `%LOCALAPPDATA%\waku\systray_state_<screen>.json`, same JSON
   format. The file lives in `kernel::appDataDir()`.
2. **One hook at a time, so no second DLL.** Waku builds the hook DLL (`hook/`) next to
   `waku.exe`. Safety net: if the pipe or mutex is already held (another tray host, such as Python
   YASB, is running), `SystrayHook::valid()` is false and the widget says "tray unavailable:
   another tray host is running" instead of doing anything to Explorer.
3. **No legacy mode.** The fallback that replaces `Shell_TrayWnd` with our own window takes over
   the tray for the whole session and has no automated test. When the hook cannot be used the
   widget reports "tray unavailable" and shows nothing.
4. **No dragging; popups and a small menu.** See the answer below.
5. **System icons** (battery, volume, network GUIDs): hidden by default, option to show them.
6. **Live testing.** The hook injects a DLL into `explorer.exe`. Everything before the live phase
   runs on fakes and the offscreen platform and never touches Explorer. The live phase needs any
   other tray host stopped first.

### Do we need drag? No.

Drag in the Widgets build did three things: move an icon between the bar and the unpinned popup
(pin and unpin), reorder icons inside a group, and show a drop marker. All three have a plainer
replacement that needs only the popups we already have:

- **Pin and unpin:** a modifier click (Ctrl by default, as in the Widgets build), or "Pin to
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
waku.services
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
                     engine that imports waku.services has it.
waku.widgets
  Tray               the widget: pinned icons, the unpinned group, the chevron button.
  TrayIconButton     one icon: image, tooltip, click handling, modifier-click to pin, its menu.
```

The raw RGBA icon cannot go through a `data:` URL the way album art does (it is not an encoded
image, and tray icons can change every second), hence the image provider. The revision in the URL
makes the `Image` reload when the icon changes.

### Pixels and blank icons

Only a message with `NIF_ICON` changes the stored pixels (`updateIconData`). `NIM_SETVERSION` and
`NIM_MODIFY` with only `NIF_TIP` or `NIF_STATE` carry no pixels; many programs send them right after
`NIM_ADD` and on every status or unread-count change. Copying their empty
pixels used to blank the icon (empty `iconUrl`). `NIF_ICON` whose handle cannot be read keeps the
last good image; `NIF_ICON` with no handle clears it.

The DLL converts the icon inside Explorer. When it sends `NIF_ICON` without pixels, the host
retries with `extractIconRgba`, which also reads monochrome icons (no colour bitmap) and records
why it failed in `TrayIconData::iconError`. A colour bitmap whose alpha bytes are all 0 has no
alpha channel; the AND mask gives its transparency.

`Systray` logs one line per icon and reason when an icon cannot show:
`waku: tray icon <exe>#<id>: <reason>`. Reasons: `no icon received yet`, the read failure (with
`, keeping the previous image` when one is kept), a pixel buffer that does not fill the icon, and
`all WxH pixels are fully transparent`. The line holds the executable's base name and the run-local
id only: no tooltip, path or handle.

### Options of the `Tray` widget

`iconSize` (16), `hide` (list of exe names), `showUnpinned` (true), `unpinnedInPopup` (false),
`iconsPerRow` (5), `showSystemIcons` (false), `pinModifier` (Ctrl), `tooltip` (true). Collapsed and
expanded button glyphs are properties of the chevron `Pill`. These are the Widgets build's options
minus the CSS ones and `use_hook` (there is only the hook).

### Threads and lifetime

As in the Widgets build: the host's worker thread parses and emits queued signals; the
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
| 1 | Extract and test | **Done** (`d74eff5`, `93eff1f`). `tray_click.*` (`sendTrayAction`, `sendTrayNotify`), `TrayIconRegistry` (identity, filter, stale sweep), `savedStateFor`, `appDataDir(folder)`. The Widgets build uses them; its 49 systray tests and the full suite (832) pass unchanged; 42 new tests. | no | M |
| 2 | `Systray` and the model | **Done.** `Systray` singleton (`click` and `doubleClick` take the icon `id`, since exe-path keys can repeat), `TrayIconModel`, `TrayImageProvider`; 19 new tests on a fake host and window API. Planned: singleton, `TrayIconModel`, the image provider (with a changing-icon test), "unavailable" states; tests with a fake `TrayHost` and `TrayWindowApi` (add, update, delete, filter, stale, click messages). | no | M |
| 3 | `TrayPins` | **Done.** `TrayPins` (instantiable, one per Tray widget; `setPinned`, `move`, `moveBy` and `isPinned` take the icon `id`) with `TrayPinList` models for `pinned` and `unpinned`, saved order and per-screen state file in the Widgets build's format. Planned: pinned and unpinned models, order, move, per-screen persistence; tests with a temp state dir and the existing state vectors. | no | S |
| 4 | `Tray` widget | **Done.** `Tray` and `TrayIconButton` (pinned row, unpinned inline or in a popup grid behind a chevron, tooltips, click, double click, Ctrl-click pin, Ctrl+right-click menu with pin, move and refresh); `TrayPins.window`/`screen`, `hidden` role and `Systray::setFactoryForTests`; 13 widget tests on the fake host. Planned: pinned row, unpinned inline and popup, chevron, tooltips, click, modifier pin, the icon menu (pin, unpin, move, refresh); offscreen tests with the fake host injected through a test seam. | no | L |
| 5 | Live bring-up | **Done 2026-10-29** on ARM64 Windows 11, Python YASB stopped (its own hook DLL, `YASBTrayHook_arm64.dll`, was loaded in Explorer; ours and the Widgets build's are the same family, Python's is a different one, so it was stopped too). The DLL already sits next to `waku.exe` (same output folder); `--no-tray` added. Found and fixed live: `Systray` never broadcast `TaskbarCreated` after the hook connected (200 ms after `updateIcons`, as the Widgets build), so no icon appeared. Checked by hand: 14 icons arrive, chevron popup with 5 columns, tooltip, Ctrl+click pins and writes `%LOCALAPPDATA%\waku\systray_state_<screen>.json`, a right click reaches OneDrive (flyout and menu), Explorer restart brings the hook and the icons back, `wakuc quit` and a forced kill both detach the DLL within 1 s. Not checked: middle and double click on a real program, order moves in the menu, other DPIs. Planned: the hook DLL copied next to `waku.exe`; `--no-tray` switch; run against the real Explorer with the Widgets build stopped: icons appear, left, right, middle and double click, tooltip, pin, order, Explorer restart, quit leaves the shell tray intact, a forced kill lets the DLL detach. | **yes** | M |
| 6 | Polish | **Done.** The icon is drawn from the provider's 32 px image with mipmaps and no `sourceSize`, so it is not shrunk before the device pixel ratio applies (the change itself was not looked at live; this machine has one 200 percent screen, so 100 and 150 percent cannot be run here). `hide` and `showSystemIcons` were done in phase 4. See History below. | no | S |

## Tests

- Phases 1 to 4 run on fakes (`TrayHost`, `TrayWindowApi`) and the offscreen platform; the service
  is not created offscreen (as in the Widgets build), so a test can never touch the real tray.
- The click function and the registry get table-driven tests (`src/features/tray/tests/`).
- Live-only (phase 5, by hand): the list above. The hook has no automated test in the Widgets
  build either, and this plan does not change that.

## Risks

| Risk | Mitigation |
|---|---|
| Injecting into Explorer disrupts the user's session | phases 1 to 4 never touch it; phase 5 only with any other tray host stopped; `--no-tray`; the hook is the one already run daily by the Widgets build; the DLL detaches within 5 s when the host's mutex goes |
| `waku.exe` starts while another tray host still holds the pipe and mutex | `valid()` is false, the widget reports it and does nothing to Explorer |
| Tray icons update very often (progress, media) | model `dataChanged` on one row, image revision bump only when the pixels changed |
| An icon with a version-4 callback behaves differently | the click function is the single place that encodes it, with tests per version |
| Undocumented Windows API use grows | none added: all of it is already in the allowlist of `docs/architecture.md`, and waku adds no call |

## History

The tray came from the Widgets build (`yasb-native`, git tag `widgets-main-final`), which owned the
hook DLL, the pipe and mutex names and the pin-state file format. Waku took them over: the DLL and
its names are now `WakuTrayHook[_arm64].dll`, `waku_systray_monitor` and `WakuTrayHookAlive`
(`WAKU_TRAY_PIPE_NAME` and `WAKU_TRAY_MUTEX_NAME` in the root `CMakeLists.txt`), pin state is in
`%LOCALAPPDATA%\waku` (the kernel copies the `systray_state_*.json` files from `YASB` once), and
the legacy host that replaced `Shell_TrayWnd` is gone. Python YASB has its own hook DLL
(`YASBTrayHook_arm64.dll`); stop it before starting the `Tray` widget, because whether the two coexist
was not tested. A regular `waku` run reserves no work area; pass `--register-app-bar` (see
`AGENTS.md`).

## Next step

None planned. Open: middle and double click on a real program, and 100 and 150 percent DPI,
were not run live.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- systray hook thread (`std::thread`) (`SystrayHook` (`src/features/tray/win32/tray_hook.h`), owned by `waku.services.Systray`, started by the first Tray widget): creates nothing itself: the GUI thread creates the watchdog mutex and the `\\.\pipe\waku_systray_monitor` pipe in the constructor; the worker injects `WakuTrayHook[_arm64].dll` into Explorer (`SetWindowsHookEx`), owns the OVERLAPPED I/O, read buffer and completion event (it waits on that event and a stop event that `stop()` sets, so an idle pipe costs no wakeups), parses each pipe message, scales a non-32 px icon to the tray size (Lanczos and sharpen, so not on the GUI thread) and emits queued signals to the GUI thread; retries after Explorer restarts (1 s no Explorer, 5 s failed injection, 3 s after the DLL disconnects); `stop()` (also called by the destructor): sets the stop flag, releases the injection hook, cancels pending I/O, releases and closes the mutex (the DLL's watchdog then detaches and restores Shell_TrayWnd's window procedure within 5 s), `join`
