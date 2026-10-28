# Follow-ups per widget

What the widget ports did not do, approximated, or could not verify. Source: the agent reports at
merge time (2026-10-28). Items marked **config** are used by the owner's live config; the rest are
Python features the config does not use. "Live" means never run against the real desktop, network
or hardware; tests use fakes. Remove an item when it is done.

## Cross-cutting

- Live A/B (screenshots, 30-minute log check, manual checklist) is still `todo` for every widget
  except cpu, memory and battery. Track it in `docs/parity-checklist.md`.
- `toggle_notification` and other `sendKeys` callbacks do not move foreground focus to Progman
  first (Python `ensure_non_elevated_foreground`); an elevated foreground window can swallow keys.
  Gap is in the shared `sendKeys` in `core/widgets/callbacks.cpp`.
- `PinnablePopup` is used for several menus; it can be pinned and dragged, which Python's cannot.
- Screenshot editor (plan unit U19) is not started.
- Undocumented-API rows in `docs/architecture.md` say "not exercised live on 26300" until verified.

## clock

- `show_holidays` / `country_code`: not ported (no C++ `holidays` equivalent). Config has it off.
- `%Z` for named IANA zones gives Qt's abbreviation (`GMT+5:30` vs Python `IST`). Config has no zones.
- Label updates on a plain `update_interval` timer, not minute/second aligned.
- Alarm/timer sound looks for `<exe dir>\assets\sound\notification02.wav`, else `SystemNotification`.
  Copy the Python asset next to the exe for the same sound.
- Calendar popup focus/click-outside comes from shared `PopupWidget`, not Python's `Qt.Popup`.
- Right-click menu top/bottom placement is inferred from screen halves (no bar-position property).
- Timer label is covered by unit tests only (no vector).

## github

- Menu structure has no Python vector; menu tests are hand-written from the Python source.
- Auth device-flow dialog is a plain `QDialog`, not styled like Python's ViewBase.
- Live: real API, the token file and the login flow were never run.

## komorebi (workspaces, active_layout)

- `app_icons` (workspaces) is covered by C++ tests with a fake window system only; the Python
  vectors have no icon scenario (they need real windows). Icons load through `TaskbarIcons`, so a
  window shows nothing (not a blank pixmap) until its icon arrives. Not viewed live.
- The layout menu now uses `MenuPopup` (Python's non-pinnable `PopupWidget`). Not viewed live.
- Two Python crash paths intentionally not reproduced (layout list missing the connected layout;
  missing `["Default"]` layout).
- Vectors use synthetic states, no recorded `komorebic state` fixture.
- Live: `komorebic` and the subscribe pipe were never run.

## media

- Flyout `show_volume_slider` (per-app volume) not ported (off by default, not in config).
- `scrolling_label` not ported (no scrolling-label widget in C++).
- Thumbnails use Qt smooth scaling, not PIL Lanczos; pixels differ slightly. Vectors record only
  pixmap presence.
- Beyond Python: `timelineInfoChanged` is re-emitted after a session switch.
- Live: only a read-only smoke test (`YASB_LIVE_MEDIA_SMOKE=1`), no session playing. Thumbnails,
  event handling and play/pause/skip commands untested.

## wifi

- WinRT connect path does not request location access (same as Python).
- No Wi-Fi interface: menu says "Wi-Fi is not available" (Python spins forever).
- Auto-connect right-click `QMenu` has no `apply_qmenu_style` equivalent.
- Host IPv4 lookup runs once at construction and then in the poll worker, not on every update.
- Live: connect, disconnect, forget, scan and the menu were never run or rendered.

## bluetooth

- LE listing uses the paired-LE selector (`FindAllAsync`), Python enumerates unfiltered; the
  listed set may differ.
- Device enrichment runs in batches of 6 and serially with refresh/connect on one backend thread.
- Live: connect/disconnect (KS audio), radio power toggle and battery reads never run; the connect
  path is the riskiest and unproven. Menu never rendered.

## control_center

- Screenshot quick action only opens `ms-screenclip:` until U19.
- Brightness: no contrast, no periodic DDC poll, no power-policy watcher; internal panel is
  re-read when the popup opens.
- DND and power-plan change listeners (WNF/registry) not ported; state re-read on open and after
  each action.
- `lock_icon` parsed but unused (as Python).
- Brightness monitor menu connector label comes from WinRT `DisplayMonitor`; built-in panel shows
  "Monitor 1 · Unknown". Not compared with Python on real hardware.
- Live: only read-only reads ran. Setters (default device, DND, power mode, touch keyboard, DDC)
  and the popup rendering are unverified.

## notifications

- `toggle_menu` popup not ported (toast list, per-app/per-toast dismiss, activation, DND button,
  app icons/images). `menu`, `icons.dnd_*`, `icons.dismiss` are accepted and ignored. Config never
  calls `toggle_menu`.
- Toast list is read only when clearing, not on every WNF change.
- Live: WNF subscription, `UserNotificationListener` and clear-all never run on real toasts;
  permission-denied path has no test.

## systray

- Live: hook DLL injection into Explorer, the legacy `Shell_TrayWnd` fallback and an Explorer
  restart were never run. Allowlist rows say "pending live run".
- "Refresh Systray" does not suppress the app bar around the broadcast (native bar ignores
  `TaskbarCreated`).
- Icons scale to 32x32 with Qt smooth scaling, not PIL Lanczos plus sharpen.
- Malformed pipe messages are dropped with a warning; Python ends the session.
- `stop()` releases the watchdog mutex on the creating thread.
- Each preset builds only its own architecture's hook DLL; x64 under emulation uses the legacy
  window (as Python).

## taskbar

- Pinned apps (pin/unpin, pinned-only buttons, `taskbar_pinned.json`, `pinned_apps_changed`) not
  ported. The owner's `%LOCALAPPDATA%\YASB` has no pinned file.
- Button drag reorder and drop-hover-to-raise (`TaskbarDropWidget`) not ported.
- Context menu has only End task, Close window, Close all windows; app-specific entries and
  "Pin to taskbar" missing. Menu position uses screen halves, not the bar position.
- Recycle Bin special icon and monitor not ported.
- Explorer special folders: `Document.Folder.Self.Path` fallback missing; id comes from
  `LocationURL` only.
- Exe icons come from the shell item image factory, not Python's resource/`.ico` extraction;
  toast-AUMID registry icon fallback skipped.
- No label text, so no label vector; only `test_taskbar_animation.py` cases are ported.
- Live: real hooks, DWM thumbnails and peek untested. Ordinal 113 `DwmpActivateLivePreview` and
  `NtQueryInformationProcess` class 60 unverified on build 26300.

## wallpapers

- Overlay stays on WorkerW after a transition until the next change or shutdown (as Python); watch
  it live.
- Thumbnail queue cancels and re-queues instead of Python's `tryTake` re-prioritisation (order may
  differ slightly). Cache keys differ from Python's, so the two builds never share thumbnails.
- `set_wallpaper_signal` event hook not ported (nothing emits it in Python).
- `accent_color: auto` uses WinRT `UISettings`, falls back to white; config uses `#fff`.
- Live: real `IDesktopWallpaper`, WorkerW attach, `foregroundMonitorRect` and
  `forceForegroundFocus` are compile-tested only.
