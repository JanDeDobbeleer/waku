# Hiding the native Windows taskbar

wabar hides the Windows taskbar while it runs and brings it back when it quits. It replaces the
separate THide tool. The option is the wabar-only root key `hide_windows_taskbar` (default `true`).

## Behaviour

- Start: hide every `Shell_TrayWnd` and `Shell_SecondaryTrayWnd` that belongs to `explorer.exe`.
- Quit (tray menu, `wabarc stop`, session end): show them again.
- Config reload: a reload restarts the process. A restart keeps the taskbar hidden and hands it to
  the replacement process, so the taskbar does not flash. If the new config turns the option off,
  the replacement restores the taskbar at start.
- Already hidden at start (for example by THide): wabar records that and never shows it again.
  It restores only what it hid itself.
- Explorer restarts (`TaskbarCreated` broadcast) and a 3 s watchdog hide a taskbar that came back.
  A monitor that appears later gets its secondary taskbar hidden by the same rescan; a screen change
  also restarts the bar, which hides again at start.
- No taskbar window found: one log line, nothing else.

## Flags

| Flag | Effect |
| --- | --- |
| `--keep-taskbar` | never hide the taskbar for this run |
| `--hide-taskbar` | allow hiding in a run with `--config-home` (isolated runs hide nothing by default) |

The real implementation is only created on the real desktop. The offscreen platform, `--screenshot`
and `--config-home` (without `--hide-taskbar`) never hide anything. Tests use a fake behind
`platform::TaskbarVisibility`.

## Mechanism

`ShowWindow(SW_HIDE)` on each taskbar window, `ShowWindow(SW_SHOWNA)` to restore.

The alternative is `SHAppBarMessage(ABM_SETSTATE, ABS_AUTOHIDE)`. It is a persistent user setting
(`StuckRects3` in the registry), so a crash would leave the user with an auto-hiding taskbar, and
the taskbar still slides in at the screen edge. `SW_HIDE` is not persistent: restarting Explorer
brings the taskbar back. That is the reason for the choice.

Ownership checks: a window counts only if it belongs to another process whose image is
`explorer.exe`. wabar's own legacy tray window (`platform/systray_legacy.cpp`) uses the class name
`Shell_TrayWnd` too and is never touched.

## Crash handling

`%LOCALAPPDATA%\wabar\taskbar_hidden.json` is written when wabar hides the taskbar and deleted when
it restores it. A file found at the next start means a crash or a restart hand-off. With the option
on, the new run takes the hidden taskbar over; with the option off, it restores it.

A hand-off is also the failure mode of a restart whose replacement never starts: the taskbar stays
hidden until the next wabar start. Explorer restarting also resets it.

## Work area and tray (from reading the code, not verified live)

- Work area: the taskbar registers itself as an app bar with Explorer. Hiding its window with
  `SW_HIDE` does not unregister it, so Explorer probably keeps the strip reserved. wabar's own app bar
  (`core/bar/app_bar.cpp`) reserves its own area on top. Possible symptom: an unused strip at the
  taskbar edge. Verify live.
- Tray hook: the DLL subclasses the real `Shell_TrayWnd` window procedure. A hidden window still
  receives its messages (`WM_COPYDATA` tray traffic), so tray icons should keep working. Verify live.

## Windows APIs

Only documented APIs: `FindWindowExW`, `GetWindowThreadProcessId`, `IsWindowVisible`, `ShowWindow`,
`RegisterWindowMessageW("TaskbarCreated")`. Nothing goes on the undocumented-API allowlist.
