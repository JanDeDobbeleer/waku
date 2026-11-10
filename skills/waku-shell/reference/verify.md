# Verification

State the level you reached for the shell and for each row. Do not claim a higher one.

| Level | Meaning | How |
|---|---|---|
| 1. Loads | The shell parses, builds its windows, reports no error | `scripts/run-candidate.ps1`: `windows=N`, `error=none` |
| 2. Looks right | Pixels match the reference within the spec | `scripts/capture.ps1` on the desktop, crops beside the reference |
| 3. Works | Each interactive element has an observable effect | Exercise it, then observe it |

Offscreen runs reach level 1 only. They show no toast, no lazily loaded panel and no pixels.

## Level 1

```powershell
pwsh -NoProfile -File skills\waku-shell\scripts\run-candidate.ps1 -ConfigHome $dir
```

Throwaway home, log file in a temp folder, and `--no-tray --no-app-bar --keep-taskbar --no-keys
--no-tiling`, so the run reserves no space and changes neither the taskbar, the keys nor the windows. It prints
the `wakuc status` line and any `warning` or `critical` log records. A QML warning at load is a
finding, not noise: fix it or explain it.

## Level 2

Run with `-Visible` (real platform, still no app bar and no tray). Then:

```powershell
pwsh -NoProfile -File skills\waku-shell\scripts\capture.ps1 -Out $dir\shot.png
pwsh -NoProfile -File skills\waku-shell\scripts\capture.ps1 -Out $dir\bar.png -Crop 0,0,3840,120
```

Look at the reference crop and your crop at the same scale. Compare numbers: bar height, gaps,
radii, colours. Pixel brightness at a point is a valid regression check (a covered widget drops
sharply). A capture that is black or empty is "not verified", not "fine".

## Level 3

For each interactive row, do the action and observe the result:

| Element | Action | Observe |
|---|---|---|
| Button, toggle | Trigger it by `wakuc call` on an `IpcHandler` that calls the same function, or ask the user | State changed in the system (volume, radio, file) or in the service |
| Shortcut | `wakuc shortcut <name>` | Panel opens or the handler runs |
| Panel, popup | `wakuc surface show <name>`, then `hide` | Shown, hidden, closes on its close rule |
| Data widget | Compare with a second source (`Get-Counter`, `powercfg`, Settings) | Values agree |
| Process launch | Run it | The process exists (`Get-Process`) |

Keypresses, UAC prompts, a physical device change and audio you hear need the user. Write the row as
`needs the user`, with the exact action to try and the expected result. Do not mark it functional.

A change to the system (volume, radio, power, files outside the throwaway home) needs the user's
say-so first. Prefer read-only checks.

## Audit

`scripts/audit.ps1` is a static check, not a test. It catches stub patterns and Linux-only calls.
Exit 0 means no pattern matched, not that the shell works.

## Report format

```
Shell: <path>   Reference: <source>
Level reached: 1 | 2 | 3 (per row below)

Element        Status                     Level  Evidence / needs the user
Clock          functional                 3      matches Get-Date
Bluetooth tray functional-with-difference 2      no per-device battery on this adapter
Idle inhibit   not possible -> dropped    -      no backend; see Dropped

Dropped: <element>: <reason>; substitute: <real one or none>
Guessed: <values>
Engine gaps: <generic feature wanted>
```
