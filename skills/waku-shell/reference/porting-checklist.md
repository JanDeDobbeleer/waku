# Porting checklist (Quickshell, Hyprland and Linux to waku)

Walk the source shell with this list. Each hit becomes a ledger row. A row without a real Windows
backend is dropped, not stubbed.

## Licence first

Read the licence of the shell. If it is not MIT, do not copy its code into the waku repository.
A user's own config home is the user's business; note the licence in the report. For this
repository, take the look and rewrite the QML.

## Imports and types

| Source | On waku |
|---|---|
| `Quickshell`, `.Io`, `.Wayland`, `.Widgets`, `.Services.Mpris/Notifications/UPower/Pipewire/SystemTray`, `.Bluetooth`, `.Networking` | Present. Behaviour limits per type: `docs/architecture.md` |
| `Quickshell.Hyprland` (`Hyprland`, `HyprlandFocusGrab`, `HyprlandMonitor`, `HyprlandWorkspace`) | Empty models, no-op `dispatch`. Replace with `Workspaces`/`Taskbar`. Drop what has no replacement |
| `WlrLayershell`, `WlrLayer` | Present. The layer maps to window stacking, not a compositor layer |
| `Quickshell.Services.Greetd`, `.Polkit`, `.I3`, `.SessionLock`, `.Notifications` extras | Check `src/quickshell/README.md`. Missing means dropped |
| `Quickshell.Wayland` `ScreencopyView`, `WlSessionLock`, `ToplevelManager.maximized` | Not available. Drop |
| `QsMenuOpener` / tray menus | Load, hold no entries. Tray items open on click only. Drop menu UI |
| `Process { command: [...] }` | Works for Windows commands. Linux commands do not exist (below) |

## Linux commands inside `Process`

Replace each with a Windows route that works, or drop the element.

| Linux | Windows route | Notes |
|---|---|---|
| `hyprctl`, `niri msg`, `swaymsg` | none | Use `Workspaces`/`Taskbar`. Drop focus/gesture features without a source |
| `nmcli`, `iwctl` | `Network`/`Wifi` service, `Quickshell.Networking` | Scan lists are limited. Check what the service exposes |
| `pactl`, `wpctl`, `amixer` | `Quickshell.Services.Pipewire` or `AudioOutput`/`AudioInput` | Per-app sessions exist |
| `playerctl` | `Mpris` | |
| `brightnessctl` | `Brightness` service | Only displays that support it |
| `systemctl poweroff/reboot/suspend` | `shutdown.exe /s /t 0`, `/r /t 0`; `rundll32 powrprof.dll,SetSuspendState` | Destructive. Confirm UI exists. Test only on request |
| `loginctl lock-session` | `rundll32 user32.dll,LockWorkStation` | |
| `notify-send` | `Notifications` service | |
| `grim`, `slurp`, `wl-copy`, `wl-paste` | none reliable | Drop unless a Windows route is tested |
| `bash`, `sh`, `awk`, `jq`, `curl` | `pwsh -NoProfile -Command ...` | Rewrite the logic. Test it |
| `xdg-open` | `Qt.openUrlExternally(url)` or `cmd /c start "" <url>` | |
| `ms-settings:` URIs | Available | Good replacement for settings panels (`ms-settings:bluetooth`, `ms-settings:sound`) |

## Paths and environment

- `~/.config`, `~/.cache`, `/usr/share`, `/etc` do not exist. Use `%LOCALAPPDATA%` and the config
  home. Read env with `Shell.env("NAME")` or `Quickshell.env`.
- Icon themes (`IconImage` by theme name) resolve from installed desktop entries. Check that the
  icons the shell shows actually render.
- Fonts: install check. A missing font silently falls back. Name a font that exists, or tell the
  user to install it.

## Keybinds and shortcuts

- Hyprland binds live in the compositor. On Windows a `GlobalShortcut` with `keys` registers the
  key, or the user binds `wakuc shortcut <name>` in whkd or AutoHotkey. Read their files for
  conflicts.
- Mouse gestures, touchpad gestures and special keys: not available. Drop.

## Behaviour that does not carry over

- Compositor blur rules, animations, window rules, gaps: the tiling is komorebi's or GlazeWM's. Ship
  a komorebi config only if the look needs it.
- Dynamic wallpaper, colour generation (matugen, wallust): only if the shell does it with a tool that
  runs on Windows. Otherwise static theme.
- Lock screens, greeters, polkit agents, screen recording: not provided. Drop.
- Detached popouts, focus grabs, scrims built on Hyprland events: rebuild with `PopupWindow` and
  explicit close rules, or drop.

## Output of this step

A table: source feature, Windows route, status (`functional`, `functional-with-difference`,
`not possible`, `engine gap`). Hand it to step 3 of the workflow.
