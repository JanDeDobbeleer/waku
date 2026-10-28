# Parity checklist

Sign-off table for the 15 widgets of the user's config. A widget is **done** when all four shared criteria hold:

1. **Label vectors pass.** The C++ test replaying `tests/vectors/labels/<widget>.json` (and the config/CSS vectors for its options) is green.
2. **A/B capture matches.** `tools/ab/Switch-Bar.ps1` captures of the Python and native bars, compared with `tools/ab/Compare-Screenshots.ps1` and the widget's masks, pass at the agreed tolerance (see `docs/baseline.md`).
3. **Manual checklist signed off by the user.** Every item in the widget's row below (popup content, hover, clicks, callbacks) behaves as in the Python build.
4. **No new log errors over 30 minutes.** `yasb-native.log` has no `ERROR` or `CRITICAL` lines that `yasb.log` lacks over a 30-minute run with the user's config.

Status values: `todo`, `in progress`, `done`. Dates are the sign-off date (YYYY-MM-DD). Plan unit in the last column.

| Widget (type) | Label vectors | A/B capture | Manual checklist (user) | 30 min log clean | Manual checklist items | Unit |
| --- | --- | --- | --- | --- | --- | --- |
| cpu (`yasb.cpu.CpuWidget`) | todo | todo | todo | todo | Label text and status class at thresholds; click toggles alt label; stat popup rows and graph; right-click callback | U8 |
| memory (`yasb.memory.MemoryWidget`) | todo | todo | todo | todo | Label sizes (binary, GNU style); alt label; stat popup; swap values | U8 |
| battery (`yasb.battery.BatteryWidget`) | todo | todo | todo | todo | Icon per threshold; charging icon and blink; time remaining phrase; hidden when no battery | U8 |
| clock (`yasb.clock.ClockWidget`) | done 2026-10-28 | todo | todo | todo | Format and alt format (invalid directives keep the raw `{...}` text); `{alarm}` icon from alarms.json; calendar popup (week numbers, year, centered); right-click menu with Set Timer, Set Alarm and the alarm list; alarm and timer dialogs; alarm fires with the active-alarm window and sound; timer label. Not ported: holidays (`show_holidays`), `%Z` abbreviations of named IANA zones | U9 |
| wifi (`yasb.wifi.WifiWidget`) | done | todo | todo | todo | Icon by signal; VPN-aware icon; menu lists networks; connect to a second network | U10 |
| bluetooth (`yasb.bluetooth.BluetoothWidget`) | done | todo | todo | todo | Icon by state; menu lists devices; connect and disconnect; device icons | U10 |
| komorebi_workspaces (`komorebi.workspaces.WorkspaceWidget`) | done 2026-10-28 | todo | todo | todo | Workspace buttons and active state (empty-label dots from CSS); click switches; scroll wraps; hides when komorebi is offline; follows whkd workspace changes; app icons are not ported (unused in the user config) | U11 |
| komorebi_active_layout (`komorebi.active_layout.ActiveLayoutWidget`) | done 2026-10-28 | todo | todo | todo | Layout icon and text; left click next layout, right click previous, middle click toggles monocle; paused/floating/maximized/monocle icons; hides when komorebi is offline; layout menu (`toggle_layout_menu`); changing layout with whkd | U11 |
| media (`yasb.media.MediaWidget`) | done 2026-10-28 | todo | todo | todo | Title and artist; thumbnail; progress bar; play/pause/next/prev; menu (Spotify and a browser tab) | U12 |
| control_center (`yasb.control_center.ControlCenterWidget`) | todo | todo | todo | todo | Every section (volume, brightness, media, toggles, power); screenshot action (interim until U19) | U13 |
| notifications (`yasb.notifications.NotificationsWidget`) | done | todo | todo | todo | Count and icon; toggle; clear; do-not-disturb state (the `toggle_menu` popup with the do-not-disturb button is not ported) | U14 |
| github (`yasb.github.GithubWidget`) | done 2026-10-28 | todo | todo | todo | Count and dot (top_right, 0/5 margin); category menu in configured order with comment counts; click opens URL and marks read; Mark all as read; right-click alt label; token from `%LOCALAPPDATA%\YASB\github_token`, device-flow login when missing. Menu structure is covered by hand-written tests (no menu vector) | U15 |
| systray (`yasb.systray.SystrayWidget`) | todo | todo | todo | todo | Icons appear through the hook; pin and unpin; popup; left/right click pass-through; Explorer restart | U16 |
| taskbar (`yasb.taskbar.TaskbarWidget`) | todo | todo | todo | todo | Grouping and count badges; add/remove animation; thumbnail preview and peek; virtual desktops; a full working session | U17 |
| wallpapers (`yasb.wallpapers.WallpapersWidget`) | done 2026-10-28 | todo | todo | todo | Empty label keeps the widget invisible but loaded; `alt+e` opens the strip gallery (portrait, 240 px, white accent) on the screen with the mouse; arrows, Page Up/Down, Home/End, wheel, double click and Enter choose; Escape or focus loss closes; split transition over WorkerW, then the wallpaper changes with no jump; right-click gallery menu sets one screen. Left click toggles the gallery, right click picks a random wallpaper. Not ported: the `set_wallpaper_signal` event-service hook (no emitter exists in Python) | U18 |

## Shared checks per widget

- Hover and pressed styles come from the user's `styles.css` selectors for the widget.
- Left, middle and right callbacks run the same actions as configured.
- Deprecated or unknown options produce the same rejection text as the Python build (config vectors).
- The widget keeps working after a config reload and after a monitor change.

## Sign-off log

| Widget | Criterion | Date | Notes |
| --- | --- | --- | --- |
| | | | |
