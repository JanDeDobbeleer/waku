# Baseline: Python YASB on the reference machine

The native build is compared against these numbers. The harness replaces and restarts the running bar, so live measurements are only run with the user present.

## Environment

| Item | Value |
| --- | --- |
| Machine | Surface Pro 11 (Windows ARM64) |
| Windows build | Windows 11 Home, build 26300 |
| YASB build | `C:\Program Files\YASB\yasb.exe`, version 2.0.8, `local/build` at `0a756c07` |
| Config | `config.yaml` SHA-256 `130FA2619798DEC53BF9C4076123E8654C2E1E7C530197BF94CFA339416352A3`; `styles.css` SHA-256 `87288ED5741A32417E3186B35E4031F9F8ECBAE01C7884FF348803358C03151D` |
| Display | Surface panel, 2880x1920, 200% DPI, top edge, 36 logical px / 72 physical px |
| Power state | AC connected, Balanced power plan; battery-saver state not independently verified |
| Date | 2026-10-27 |

This is a four-run baseline (`N=4`), rather than the planned five. A display change after run 4 caused YASB's normal screen-change restart during run 5. The user chose to keep the four completed runs; all four use the same Surface display and settings. The later external-display state is not included.

## Measurement procedure

Use Windows PowerShell 5.1 from `tools\ab`. Close other apps; keep the machine idle and the lid and display on.

1. Dry-run first: `.\Switch-Bar.ps1 -Target python -WhatIf` and read the plan.
2. For each of N = 4 runs: `.\Switch-Bar.ps1 -Target python -Label run<N>` (stops the bar, waits for the tray hook DLL to leave `explorer.exe`, starts the bar through explorer, waits for the `Created on Screen` log line, waits 5 s, captures the bar). Note `readySeconds` from the JSON next to the capture.
3. After the capture of each run, leave the machine alone and run `.\Measure-Bar.ps1 -Target python -IdleMinutes 10 -SwitchRecord out\<capture>.json -Label run<N>`.
4. Create `tools\ab\masks.json` from `masks.sample.json` (clock, CPU and memory values, media title, tray icons, battery, anything that changes between runs). Check coordinates against a capture.
5. Compare the captures of the Python build with each other: `.\Compare-Screenshots.ps1 -Reference out\python-...run1.png -Candidate out\python-...run2.png -MaskFile masks.json`.

## Noise tolerance

The Python build compared with itself defines what "the same" means.

1. Compare every pair of the N Python captures with the current masks, at `-ChannelTolerance 0` and `-MaxDiffPercent 0`. Record the differing share of each pair.
2. If any pair differs outside the masks, look at the `-DiffImage` output. Widen the masks only for content that is genuinely live (time, rates, titles). Do not widen them over static chrome.
3. Pick `-MaxDiffPercent` as the largest differing share among all Python-vs-Python pairs, rounded up, plus 50 % headroom, but not above 0.5 %. Pick `-ChannelTolerance` as the smallest value (from 0, 4, 8, 16) under which the Python pairs pass with that share. Record both below.
4. For timing and memory, the tolerance is the spread of the N runs: report median and range per metric. A native result counts as at parity when its median is within the Python range, or better.
5. Sanity check of the harness: `Switch-Bar.ps1 -Target python` twice (same build, with a delay as long as a real swap) must produce captures that compare as identical.

## Results (Python build)

| Metric | Run 1 | Run 2 | Run 3 | Run 4 | Run 5 | Median | Range |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Time to bar visible (s) | 1.90 | 1.74 | 1.91 | 1.77 | — | 1.84 | 1.74–1.91 |
| Private bytes, median over window (MiB) | 100.8 | 100.5 | 100.4 | 100.2 | — | 100.5 | 100.2–100.8 |
| Private bytes, end of window (MiB) | 100.6 | 100.5 | 101.2 | 100.3 | — | 100.5 | 100.3–101.2 |
| Idle CPU, 10 min (% of one core) | 3.338 | 3.394 | 3.759 | 3.651 | — | 3.523 | 3.338–3.759 |
| Context switches per second | N/A | N/A | N/A | N/A | — | N/A | N/A |
| Handles / threads at end | 1049 / 44 | 1017 / 41 | 1064 / 44 | 1031 / 45 | — | 1040 / 44 | 1017–1064 / 41–45 |

The Windows process context-switch counter is unavailable on this machine. `Measure-Bar.ps1` now warns and writes `null` instead of reporting a false zero.

## Screenshot tolerance chosen

| Setting | Value |
| --- | --- |
| `-ChannelTolerance` | 0 |
| `-MaxDiffPercent` | 0 |
| Largest Python-vs-Python differing share | 0.0000% across all six pairs |
| Mask file | `tools\ab\masks.json` (machine-local and git-ignored) |

## Results (native build)

Filled in per milestone with the same procedure and `-Target native -NativeExe <path>`.

| Milestone | Date | Time to bar visible (s) | Private bytes (MB) | Idle CPU (% of core) | Context switches/s | Screenshot diff (%) |
| --- | --- | --- | --- | --- | --- | --- |
| U8 (cpu, memory, battery only) | 2026-10-27 | not measured | 19.6 (Python 62.7) | 0.19 before font fix (Python 0.41); re-measure | N/A | visually identical; values masked |

### U8 A/B run notes

Simplified run on 2026-10-27: both builds on the same three-widget copy of the live config
(`build\gate-config-u8`, Python via `YASB_CONFIG_HOME`, native via `--config-home --register-app-bar`),
BenQ RD280UG 3840x2560 at 150%, one run each, 5 minutes after 30 s settle.

| Metric | Python | Native, DirectWrite (first run) | Native, GDI (after fix) |
| --- | --- | --- | --- |
| Private bytes (MiB) | 62.7 | 79.3 | 19.6 |
| Working set (MiB) | 106.1 | 206.0 | 51.9 |
| Handles | 468 | 2,105 | 426 |
| Threads | 16 | 17 | 18 |

The first native run left Qt on its default DirectWrite font database, which memory-maps every installed font
(1,067 here: 863 File + 853 Section handles). Python YASB forces `QT_QPA_PLATFORM=windows:fontengine=gdi`
(`src/env.py`, override `YASB_FONT_ENGINE`); `app/main.cpp` now does the same. Handle counts stayed flat over
90 s, so there is no leak. The post-fix visual check and CPU re-measure are still open.
| Full parity | PENDING | PENDING | PENDING | PENDING | PENDING | PENDING |
