# Follow-ups per widget

What the widget ports did not do, approximated, or could not verify. Source: the agent reports at
merge time (2026-10-28). Items marked **config** are used by the owner's live config; the rest are
Python features the config does not use. "Live" means never run against the real desktop, network
or hardware; tests use fakes. Remove an item when it is done.

## Cross-cutting

- Live A/B (screenshots, 30-minute log check, manual checklist) is still `todo` for every widget
  except cpu, memory and battery. Track it in `docs/parity-checklist.md`.
- `PinnablePopup` is used for several menus; it can be pinned and dragged, which Python's cannot.
- Screenshot editor (plan unit U19) is not started.
- Undocumented-API rows in `docs/architecture.md` say "not exercised live on 26300" until verified.

## clock

- `show_holidays` / `country_code`: not ported (no C++ `holidays` equivalent). Config has it off.
- Calendar popup focus/click-outside comes from shared `PopupWidget`, not Python's `Qt.Popup`.
- `%Z` uses a table of common IANA zones plus a numeric fallback (`+04`), not the full tzdata.

## github

- Menu structure has no Python vector; menu tests are hand-written from the Python source.
- Auth dialog approximates ViewBase: no window icon, no dark title bar on Windows 10, no live
  theme switch while open.
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

- Thumbnails use a Pillow-style Lanczos-3 resample (`widgets/media/lanczos.cpp`); pixels can still
  differ by rounding. Vectors record only pixmap presence.
- `show_volume_slider` and `scrolling_label` are ported but only covered by fakes (no live audio
  session or bar check). `scrolling_label.ease_min` is honoured; Python reads the wrong key
  (`ease_min_value`) and always uses 0.5. An explicit empty `scrolling_label.separator` becomes a
  space (the generated struct cannot keep the schema's `" "` default).
- Beyond Python: `timelineInfoChanged` is re-emitted after a session switch.
- Live: only a read-only smoke test (`YASB_LIVE_MEDIA_SMOKE=1`), no session playing. Thumbnails,
  event handling and play/pause/skip commands untested.

## wifi

- WinRT connect path does not request location access (same as Python).
- No Wi-Fi interface: menu says "Wi-Fi is not available" (Python spins forever).
- Live: connect, disconnect, forget, scan and the menu were never run or rendered.

## bluetooth

- Device order in the menu and the "Connected devices" tooltip is sorted by address; Python keeps
  enumeration order (classic first, then LE). Duplicate names in the tooltip come from distinct
  addresses (a device that is both classic and LE) and match Python's dedupe-by-address.
- Device enrichment runs in batches of 6 and serially with refresh/connect on one backend thread.
- Live: connect/disconnect (KS audio), radio power toggle and battery reads never run; the connect
  path is the riskiest and unproven. Menu never rendered.

## control_center

- Screenshot quick action only opens `ms-screenclip:` until U19.
- Brightness contrast (VCP 0x12) is read, written and polled by the service but, as in Python, the
  control center has no contrast slider. The 60 s DDC poll and the power-policy watcher are ported;
  neither has run against a real DDC monitor or a live scheme change.
- DND and power-plan state are re-read on popup open and after each action. Python's control center
  does not subscribe to WNF or the registry either (only the dnd, notifications and power_plan
  widgets do), so no listener is added.
- `lock_icon` parsed but unused (as Python).
- Brightness monitor menu connector label matches Python's enum names (checked against the winrt
  projection: Unknown, Hd15, Analog T V, Dvi, Hdmi, Lvds, Sdi, Display Port); the built-in panel
  shows "Monitor 1 · Unknown" in Python too.
- Live: only read-only reads ran. Setters (default device, DND, power mode, touch keyboard, DDC)
  and the popup rendering are unverified.

## notifications

- App icons in the menu come from `IShellItemImageFactory` for `shell:AppsFolder\<aumid>`,
  not Python's `IconService`; a header built before its icon arrived is rebuilt once the
  icon is ready (Python waits for the next refresh).
- Toast activation (`INotificationActivationCallback`, `show_app`), per-toast/per-app
  dismiss, the Focus Assist write and the quiet-hours WNF subscription never ran on real
  toasts. Live: WNF subscription, `UserNotificationListener` and clear-all too.
- The menu has no parity vector (the Python exporter only covers the label); the tests
  are hand-written from the Python source.

## systray

- Live: hook DLL injection into Explorer, the legacy `Shell_TrayWnd` fallback and an Explorer
  restart were never run. Allowlist rows say "pending live run".
- "Refresh Systray" has nothing to suppress: the native bar has no `TaskbarCreated` handler, so
  it also does not re-register the app bar after an Explorer restart (Python does).
- Icon scaling follows Pillow's Lanczos plus SHARPEN but is not verified pixel-exact against PIL.
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
- Cache keys differ from Python's, so the two builds never share thumbnails. (Queued thumbnail
  jobs are re-prioritised by cancelling and restarting them, which matches `tryTake` + `start`.)
- `set_wallpaper_signal` event hook not ported (nothing emits it in Python).
- `accent_color: auto` uses WinRT `UISettings`, falls back to white; config uses `#fff`.
- Live: real `IDesktopWallpaper`, WorkerW attach, `foregroundMonitorRect` and
  `forceForegroundFocus` are compile-tested only.
