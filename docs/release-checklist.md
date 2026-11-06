# Release checklist: live-only behaviour

Some Windows code runs only on a live desktop. Tests cover it through fakes or not at all, so a
release needs a hand check of each line below. Run each check on a test config home
(`waku.exe --config-home <dir>`), never on `%USERPROFILE%\.config\waku`. Update the date in the
last column when a check passes, and say on which Windows build.

The undocumented API rows in `docs/architecture.md` that say "not exercised live" point here.
The automated part of a release is `tools\check.ps1` on `arm64-debug` plus `ctest` on
`arm64-release` and `x64-release` (`docs/building.md`).

| Behaviour | Code | How to exercise it | Last seen working |
|---|---|---|---|
| App bar reservation (`ABM_NEW`, `ABM_QUERYPOS`, `ABM_SETPOS`) | `src/host/win32/app_bar.cpp` | Start waku normally. Maximise a window: it stops at the bar. `wakuc quit` gives the area back. | 2026-10-30, build 26300, ARM64 (left edge, scale 0.75) |
| Windows taskbar hide and restore | `src/host/windows_taskbar.cpp`, `src/host/win32/windows_taskbar_win.cpp` | `wakuc taskbar hide`, `status`, `show`. Then hide, end `waku.exe` in Task Manager and start it again: the stale marker restores the taskbar. | hide, show, status: 2026-10-30, build 26300; crash recovery: never |
| Native window hooks: topmost, `WS_EX_NOACTIVATE`, the cursor poll of `mask:` | `src/host/win32/shell_host_win32.cpp` (`NativeHooks`) | A shell with a `Region` mask and a popup. Click the bar while another app has focus: the focus stays. Clicks in the empty region reach the window below. Repeat on two screens with different scale. | one screen: 2026-10-30, build 26300 (celeste); mixed DPI: never |
| DWM backdrop and dark mode | `src/host/win32/backdrop.cpp` | A window with each `Backdrop` kind (`Blur`, `Mica`) on Windows 11 and, if at hand, Windows 10. | not recorded |
| Global hotkeys (`RegisterHotKey`) | `src/kernel/qt/hotkeys.*`, `src/kernel/win32/hotkeys_win.cpp` (used by `src/host/qml/global_shortcut.*` and the wallpaper gallery) | A `GlobalShortcut` bound to a key; press it from another app. The wallpaper gallery hotkey (`alt+e` by default). `--no-hotkeys` registers none. | not recorded |
| Keyboard focus for a shown surface | `src/kernel/win32/window_focus.cpp` | Bind `wakuc surface show <name>` to a key in whkd or AutoHotkey (`examples/launcher`). Typing goes to the launcher at once. | not recorded |
| Taskbar pins and `shell:::{...}` verbs | `src/features/taskbar/win32/pin_support_win.cpp` | Right-click a taskbar button, "Pin to taskbar"; restart waku: the pin stays and launches. Pin an `.msc` tool (Event Viewer) and the Recycle Bin; unpin them. The id parser has unit tests (`test_taskbar_platform.cpp`). | not recorded |
| Tray hook in Explorer | `src/features/tray/win32/tray_hook.cpp`, `src/features/tray/hook/trayhook.cpp` | Start without `--no-tray`. The icons arrive and clicks reach the apps. Restart Explorer: the tray recovers. `wakuc quit` and a forced kill detach the DLL within 1 s. | 2026-10-29, build 26300, ARM64 |
| Wallpaper transition on WorkerW | `src/features/wallpapers/win32/workerw.cpp` (`attachToWorkerW`) | A `Wallpapers` widget with `engine: true`. `wakuc wallpaper random --dry-run` plays the animation behind the desktop icons and does not set the wallpaper. | never on build 26300 |
| Wallpaper fit and transcode limits | `src/features/wallpapers/win32/workerw.cpp` (`readWallpaperFit`, `readTranscodeLimits`) | The same transition with each fit in Settings (Fill, Fit, Stretch, Tile, Span) and with a panorama image: the last frame matches the wallpaper Windows shows. | never on build 26300 |
| Lock screen picture | `src/features/wallpapers/win32/lock_screen_win.cpp` | `wakuc wallpaper next` (no dry run), then Win+L: the lock screen shows the same image. | not recorded |
| Touch keyboard (`ITipInvocation`) | `src/features/control_center/win32/touch_keyboard_win.cpp` | The ControlCenter keyboard toggle, once with TabTip running and once after ending it. | never |
| Power mode setters | `src/features/control_center/win32/power_win.cpp` (`setPowerMode`) | The ControlCenter power mode button; Settings > Power shows the new mode. | getter: build 26300 (date not recorded); setters: never |
| Default audio device (`IPolicyConfig`) | `src/features/audio/win32/audio_endpoints_win.cpp` (`setDefaultDevice`) | The audio source menu: choose another output. Sound settings show it as the default. | never |
| Per-app audio sessions | `src/features/audio/win32/audio_streams_win.cpp` | A shell with `AudioStreams`. Play sound in two apps: both rows appear and their volume sliders work. | never |
| Bluetooth audio connect and disconnect | `src/features/bluetooth/win32/bluetooth_audio_win.cpp` (`setAudioConnection`) | The Bluetooth menu on a paired headset: disconnect, then connect. | never |
| Do Not Disturb write and watch | `src/features/notifications/win32/dnd_win.cpp` | The ControlCenter DND tile. Then toggle DND in the Windows notification centre: the tile follows. | read: build 26300 (date not recorded); write and watch: never |
| App and toast activation | `src/features/apps/win32/launch_win.cpp`, `src/features/notifications/win32/toast_activation_win.cpp`, `src/kernel/win32/app_activation.cpp` | Launch a Store app and a desktop app from a shell that lists `DesktopEntries`, and "run as administrator" where the shell offers it. Click a toast in the notification list. | not recorded |
| Desktop state probes and GPU counters | `src/features/desktop_state/win32/desktop_probe_win.cpp`, `src/features/system_stats/win32/` | A shell that shows `DesktopState` and `Gpu`. Start a full-screen game or video: the values change; compare the GPU load with Task Manager. | never |
| MSIX install and update | `packaging/msix/` | Install `waku-<arch>.msix` from a release with `Add-AppxPackage`. `waku` and `wakuc` resolve from a terminal. `wakuc quit` works against a bar started from the Start entry. Install the next version over it: the bar restarts on the new one. | never |
| MSIX does not redirect writes | `packaging/msix/appxmanifest.xml` | Pin a taskbar button from the installed waku. `%LOCALAPPDATA%\waku` (not `%LOCALAPPDATA%\Packages\JanDeDobbeleer.Waku_*`) holds the pin file. | never |
| Tray hook from the package folder | `src/features/tray/win32/tray_hook.cpp` | The installed waku (without `--no-tray`) loads `WakuTrayHook*.dll` from `C:\Program Files\WindowsApps\...` into Explorer. Icons arrive. | never |
