# Baseline: Python YASB on the reference machine

The native build is compared against these numbers. All values are **PENDING** until measured live with the user present: the harness replaces and restarts the running bar, so it is never run unattended.

## Environment (fill in at measurement time)

| Item | Value |
| --- | --- |
| Machine | Surface Pro 11 (Windows ARM64) |
| Windows build | PENDING |
| YASB build | `C:\Program Files\YASB\yasb.exe`, version PENDING, commit PENDING |
| Config | the user's `config.yaml` and `styles.css` (copy hashes: PENDING) |
| Display | resolution PENDING, scale PENDING, bar edge and height PENDING |
| Power state | PENDING (plugged in, balanced, no battery saver) |
| Date | PENDING |

## Measurement procedure

Use Windows PowerShell 5.1 from `tools\ab`. Close other apps; keep the machine idle and the lid and display on.

1. Dry-run first: `.\Switch-Bar.ps1 -Target python -WhatIf` and read the plan.
2. For each of N = 5 runs: `.\Switch-Bar.ps1 -Target python -Label run<N>` (stops the bar, waits for the tray hook DLL to leave `explorer.exe`, starts the bar through explorer, waits for the `Created on Screen` log line, waits 5 s, captures the bar). Note `readySeconds` from the JSON next to the capture.
3. After the capture of each run, leave the machine alone and run `.\Measure-Bar.ps1 -Target python -IdleMinutes 10 -SwitchRecord out\<capture>.json -Label run<N>`.
4. Create `tools\ab\masks.json` from `masks.sample.json` (clock, CPU and memory values, media title, tray icons, battery, anything that changes between runs). Check coordinates against a capture.
5. Compare the captures of the Python build with each other: `.\Compare-Screenshots.ps1 -Reference out\python-...run1.png -Candidate out\python-...run2.png -MaskFile masks.json`.

## Noise tolerance

The Python build compared with itself defines what "the same" means.

1. Compare every pair of the N Python captures with the current masks, at `-ChannelTolerance 8` and `-MaxDiffPercent 0`. Record the differing share of each pair.
2. If any pair differs outside the masks, look at the `-DiffImage` output. Widen the masks only for content that is genuinely live (time, rates, titles). Do not widen them over static chrome.
3. Pick `-MaxDiffPercent` as the largest differing share among all Python-vs-Python pairs, rounded up, plus 50 % headroom, but not above 0.5 %. Pick `-ChannelTolerance` as the smallest value (from 0, 4, 8, 16) under which the Python pairs pass with that share. Record both below.
4. For timing and memory, the tolerance is the spread of the N runs: report median and range per metric. A native result counts as at parity when its median is within the Python range, or better.
5. Sanity check of the harness: `Switch-Bar.ps1 -Target python` twice (same build, with a delay as long as a real swap) must produce captures that compare as identical.

## Results (Python build)

| Metric | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 | Median | Range |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Time to bar visible (s) | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
| Private bytes, median over window (MB) | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
| Private bytes, end of window (MB) | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
| Idle CPU, 10 min (% of one core) | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
| Context switches per second | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
| Handles / threads at end | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |

## Screenshot tolerance chosen

| Setting | Value |
| --- | --- |
| `-ChannelTolerance` | PENDING |
| `-MaxDiffPercent` | PENDING |
| Largest Python-vs-Python differing share | PENDING |
| Mask file | `tools\ab\masks.json` (PENDING, keep out of git if it contains personal layout) |

## Results (native build)

Filled in per milestone with the same procedure and `-Target native -NativeExe <path>`.

| Milestone | Date | Time to bar visible (s) | Private bytes (MB) | Idle CPU (% of core) | Context switches/s | Screenshot diff (%) |
| --- | --- | --- | --- | --- | --- | --- |
| U8 (cpu, memory, battery only) | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
| Full parity | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
