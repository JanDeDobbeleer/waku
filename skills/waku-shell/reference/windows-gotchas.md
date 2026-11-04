# Windows gotchas

Each item comes from real port work (caelestia, noctalia, omarchy). Read before you write layout,
keys, launch code or panels.

## Services and types

- **Null until ready.** `Bluetooth.defaultAdapter` is `null` until the service reports. A panel with
  `visible: adapter !== null` never showed. Show the panel; make its content depend on the state.
- **Workspace ids are strings** such as `DISPLAY1:0`. `property int` fails with "Cannot assign
  QString to int". Use `var`.
- **Do not name your file after a service.** `Wallpapers.qml`, `Workspaces.qml`, `Bluetooth.qml`,
  `Media.qml` beside `shell.qml` are your own types.
- **Redeclared properties shadow the engine's.** A window that declares `screen` keeps the engine's
  placement on the primary screen. Pass the screen under another name.
- **No Hyprland data.** `Hyprland.workspaces` and friends are empty. Use `Workspaces` and
  `Taskbar` (komorebi or GlazeWM). With GlazeWM `windows` is empty.
- **Bluetooth names.** A device may show its address until Windows resolves a name. Do not hardcode
  names. Display the name the service reports and fall back to the address.

## Windows and focus

- **Keyboard focus.** The engine activates a panel when it becomes visible with keyboard focus and
  when `keyboardFocus` changes while visible. If a launcher does not take the keys, test the
  change first (`wakuc surface show <name>`). Do not add a timer workaround before you know the
  engine build has the fix.
- **Stacking.** A full-screen window and a separate bar window can cover the bar when focus moves.
  A bar drawn inside the same window as the drawer does not have this problem. If you need two
  windows, test open and close of every panel, then compare the bar's pixels before and after.
- **Click-through masks** (`mask: Region`) are enforced by polling the cursor. Pad targets a few
  pixels. Hover outside the mask does not run.
- **Live window previews** (`WindowThumbnail`) are drawn by the compositor above the window. Nothing
  overlays them.
- **Windows 11 rounds window corners by 8 logical px** and komorebi's border follows. A per-window
  radius cannot be set. Match 8 px where the shell frames tiled windows.
- **Auto-hide docks** leave a 1 px hover window at the screen edge. It reads as a stray bar.
- **Detach and focus-grab popups** (`HyprlandFocusGrab.onCleared`) never fire. A popup built on them
  cannot be closed. Close popups on `Escape`, outside click, or `FocusScope`/`activeFocus` loss.

## Windows tiling (komorebi)

- komorebi adds its own padding (about 4 to 6 px) and border (about 2 px). A shell that wants
  windows to sit in a frame ships a `komorebi.json`. Measure with `komorebic state` window
  rectangles, not by eye.
- Do not write `komorebi.json` into the user's real komorebi folder. Ship it in the shell and let
  the user apply it. Back up before applying.
- `--manage-komorebi` runs `komorebic replace-configuration` after screen or scale changes. Opt in.

## Keys and launching

- **Check for key owners first.** whkd (`whkdrc`) and AutoHotkey grab keys system-wide. Alt+Shift+
  Enter was `komorebic promote`. Read the user's files and avoid taken combinations.
- **Store apps** (Windows Terminal) have an AUMID and an execution alias. `execute()` raises a
  running one. Use `executeNew()` to start another and `launchAsAdmin` for admin.
- **Elevation** shows UAC. You cannot answer it. Mark the row `needs the user`.
- A tool call that starts `waku.exe` in the foreground blocks on the output pipe. Start it as a
  background process.

## Environment and startup

- Run with `--config-home`. Ports that need environment variables (a config dir, `PATH` entries such
  as komorebic's folder) set them in a start script. A scheduled task does not run that script:
  tell the user to set the variables in their user environment.
- `wakuc startup enable` registers a per-user logon task. It replaces any earlier task and deletes
  `waku.lnk` in the Startup folder. It bakes in one config home. A bare `waku.exe` resolves on the
  system `PATH`. Tell the user before you run it. Do not run it unasked.
- Restart order for a live bar: `wakuc --config-home <dir> quit`, then stop komorebi with
  `komorebic stop --whkd` if the shell uses it, then start the shell.

## Screenshots

- Capture DPI-aware (`scripts/capture.ps1`). A capture from a DPI-unaware process is scaled down
  (2560x1707 instead of 3840x2560) and makes a working bar look missing.
- A scheduled-task or tool-launched process may not see the interactive desktop. If a capture is
  black or empty, say so. Do not conclude from it.

## Performance

- A `Window` or `PopupWindow` instantiated per button, tooltip or tray icon is costly (several
  `QQuickWindow` objects each). Use `Loader { active: false }` or `LazyLoader` and activate on
  demand.
- Gate bindings of hidden popups on their visibility.
- Transitions and large blurred layers cost memory and GPU. Add them only when the reference shows
  them.

## Logging

- Start with `--log-file <path>` to diagnose (`docs/logging.md`). The engine may not log every
  result: "no errors in the log" does not mean it worked. Check the behaviour.
