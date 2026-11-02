# Memory and start-up profile, 2026-10-31

Lane 1a of batch 1 in `2026-10-31-cpp-review.md`. Measured on the user's laptop (Snapdragon X,
Adreno X1-85, one screen 2880x1920 at 200 %, Windows 11 26300) with the caelestia port, before and
after the lane 1a changes.

## Verdict

GPU memory dominates. The D3D11 textures that Qt Quick allocates for the windows, their item layers
and the shell's effects are counted in the process's private bytes (the GPU is UMA: "GPU shared
usage" is system memory). With the shell idle they are 134 of 392 MB private; after the launcher and
the dashboard have been opened once they are 499 of 849 MB, and they stay there until a reload. The
rest is the CRT heap (about 240 MB: the QML engine, compiled QML, the scene graph, images) and
about 120 MB of other private allocations. The production figure of about 1 GB is the idle shell
plus the panels opened once plus the tray, the real wallpaper and an hour of uptime.

The one engine knob that pays is the depth buffer: 52-54 MB less in every phase. Neither the render
loop, the D3D12 backend nor fewer windows changes much. The rest of the GPU memory is the shell's
own layers and effects, which the port controls, not the engine.

Start-up: the probe engine compiled the whole shell before the real engine (818 ms of 3.1 s).
Skipping it on the first load, and one config-home walk instead of two, bring process start to
first frame from a median 3.7 s to 2.8 s over six interleaved starts. Reloads are slow for a
different reason: `clearSingletons()` takes 2.5 s, and in one or two of ten reloads 32 s. A stack
taken during it points at the `Quickshell` singleton reading the system clipboard in a binding
(see the findings).

Lane 1a result, release build: idle private bytes 390 -> 336 MB, with the panels opened 849 ->
796 MB, after reloads 471 -> 419 MB; start to first frame 3.7 -> 2.8 s (medians).

## Method

- Engine: a copy of the deployed `dist\waku` (`cdcc88a`) for the baseline experiments; for the
  code changes, a release build of the review worktree (`arm64-release`, waku and wakuc only)
  copied over the same deployment, so only `waku.exe` differs between before and after.
- Shell: a worktree of the caelestia port (`D:\waku-ports\wt\memprobe`, branch `memprobe` at
  `3e90c0d`), never the production config home.
- Private instances only: `--config-home <copy> --import-path <copy>\modules --no-tray
  --font-engine=directwrite`, no `--register-app-bar` and no `--hide-taskbar`;
  `CAELESTIA_DRYRUN=1`, `WAKU_WALLPAPER_DRYRUN=1` and a private `LOCALAPPDATA` (warm QML disk
  cache after the first run).
- Numbers: `Get-Process` (private bytes, working set, threads, handles), the
  `\Process V2(...)\Working Set - Private` counter and the `\GPU Process Memory(...)` counters
  (dedicated and shared usage). Committed memory by usage from `cdb -pv` and `!address
  -f:MEM_COMMIT` (VMMap is not installed). Timing from `wakuc status` and, after the first commit
  of this lane, from the `WAKU_TRACE=1` lines.
- Phases: ready, idle after 30 s, launcher open (`wakuc call launcher open`), dashboard open
  (`wakuc call bar dashTab 0`), both closed, then reloads (`wakuc reload`). The scripts are in
  the job folder (`mp\lib.ps1`, `phase1.ps1`, `exp.ps1`, `bench.ps1`).
- A screen grab to compare the rendering with and without the depth buffer was not possible: the
  session was locked, and `CopyFromScreen` returned black frames.

## Baseline (deployed engine, `cdcc88a`)

| Phase | Private MB | Private WS MB | Working set MB | GPU shared MB | Threads | Handles |
|---|---|---|---|---|---|---|
| `--safe` (built-in shell) | 61 | 30 | 90 | 8 | 19 | 760 |
| shell ready | 406 | 358 | 453 | 137 | 55 | 1394 |
| shell idle 30 s | 402 | 351 | 462 | 139 | 67 | 1599 |
| launcher open | 820 | 769 | 884 | 509 | 62 | 1632 |
| launcher closed | 779 | 728 | 843 | 509 | 62 | 1631 |
| dashboard open | 956 | 899 | 1113 | 597 | 66 | 3390 |
| dashboard closed | 955 | 898 | 1112 | 597 | 66 | 3412 |
| reload 1 | 503 | 457 | 666 | 138 | 71 | 3418 |
| reloads 2-10 | 481-488 | 268-437 | 478-647 | 138-141 | 63-69 | 3398-3450 |

The launcher's jump differs between runs (+76 to +370 MB GPU, depending on what its lists have
loaded 4 s after the open); after both panels the total is the same in every run (about 849 MB
private, 498 MB GPU). The production instance at the time: 982 MB private, 795 MB working set,
70 threads, 3747 handles.

Committed memory by usage, idle and launcher open (`!address -f:MEM_COMMIT`):

| Usage | Idle MB | Launcher open MB |
|---|---|---|
| Image (DLLs, mostly shared) | 387 | 458 |
| Heap (CRT and process heaps) | 243 | 249 |
| Mapped files | 138 | 138 |
| Private, not heap (GPU allocations, V4 JS heap, thread pools) | 117 | 487 |
| Stacks | 9 | 10 |

The launcher's 370 MB lands entirely in "private, not heap" and matches the GPU shared counter.

## Experiments (deployed engine, environment knobs, one fresh process each)

| Knob | Idle private MB | Idle GPU MB | Panels opened private MB | Panels opened GPU MB | Threads idle |
|---|---|---|---|---|---|
| none (two runs) | 390-391 | 132-133 | 849-850 | 498 | 59-60 |
| `QSG_RENDER_LOOP=basic` | 389 | 132 | 850 | 498 | 57 |
| `QSG_NO_DEPTH_BUFFER=1` (two runs) | 337-338 | 80 | 796 | 446 | 58-61 |
| `QSG_NO_DEPTH_BUFFER=1` + `QSG_NO_STENCIL_BUFFER=1` | 335 | 80 | | | 58 |
| `QSG_RHI_BACKEND=d3d12` | 790 | 438 | 918 | 473 | 59 |
| `QT_QUICK_BACKEND=software` | 284 | 0 | 379 | 0 | 56 |
| `QML_DISABLE_DISK_CACHE=1` | 395 | 133 | | | 59 |
| one window (the four 1x1 `ExclusionZone` windows removed) | 369 | 130 | | | 56 |

What dominates, in order:

1. Item layers and effects of the shell (GPU, 300-370 MB once the panels have been opened). The
   software backend, which has no GPU textures, holds 379 MB with both panels opened against
   849 MB with D3D11. The textures are allocated on first open and kept while the items live; the
   dashboard and launcher content stays instantiated after closing. Shell-side: `layer.enabled` on
   full-window items (`ContentWindow.qml:220`, a full-screen layer with a `MultiEffect` shadow, is
   always on), `MultiEffect`/`Mask` layers in the dashboard (`User.qml`, `CoverArt.qml`,
   `LyricList.qml`), and panels that are hidden but not unloaded.
2. The CRT heap, about 240 MB at idle: the QML engine, the compiled shell (308 QML files under
   `modules`), the scene graph and images. The disk cache does not change it.
3. Images of DLLs (387-458 MB committed, mostly shared and not in private bytes). The launcher
   loads another 70 MB of DLLs (shell extensions and codecs for app icons).
4. The swap chain and depth buffers: 52 MB for the depth buffers (fixed below); a full-screen BGRA
   buffer is 22 MB at 2880x1920.

What does not matter here: the threaded render loop (five `QRhi`s on the Adreno cost about nothing;
the basic loop saves two threads and no memory), the four 1x1 exclusion windows (21 MB and four
threads together), and the QML disk cache (memory; it matters for start-up).

`QSG_RHI_BACKEND=d3d12` doubles the idle memory (438 MB GPU) and must not be used.

## Start-up and reloads (release build, `WAKU_TRACE=1`)

Before (`e34ce54`, the trace commit), start 3 of 3:

| ms | Step |
|---|---|
| 522 | application built (`QGuiApplication`, the platform plugin and the DirectWrite font database; 20 ms in the other runs, so mostly disk) |
| 547 | engine built, import roots checked |
| 593 | reload started |
| 601 | config plugins checked |
| 1419 | shell checked in the probe engine (818 ms) |
| 1499 | engine reset |
| 2180 | shell compiled (681 ms) |
| 3683 | shell created (1503 ms) |
| 3733 | shell parts activated |
| 3757 / 3799 | config home scanned (24 ms) / watched (42 ms) |
| 3981 | first frame of each of the five windows |

A hot reload (after the destroy and singleton trace points, `aad4dea`):

| ms after `reload started` | Step |
|---|---|
| 14 | config plugins checked |
| 673 | shell checked in the probe engine (659 ms) |
| 757 | old shell destroyed (84 ms) |
| 3273 | singletons cleared (2516 ms) |
| 3571 | engine reset (component cache cleared, 298 ms) |
| 4527 | shell compiled (956 ms) |
| 5637 | shell created (1110 ms) |
| 5700 | shell parts activated |

Two of ten reloads took 35.7 s and 36.0 s; both times `clearSingletons()` took 32-33 s. Each
reload costs about 6 s of CPU across threads. The log shows ten `Failed to initialize COM library
(Cannot change thread mode after it is set.)` lines per reload and a GlazeWM reconnect line right
before the slow ones.

## Changes (lane 1a)

| Commit | Change | Before | After |
|---|---|---|---|
| `e34ce54` | `WAKU_TRACE=1` start-up and reload steps | | |
| `aad4dea` | trace the destroy and singleton steps of a reload | | |
| `6d5f8af` | skip the probe-engine check on the first load (C35) | reload started -> parts 3140 ms, first frame 3.06-4.19 s | 2142 ms, first frame 2.36-2.88 s |
| `5e36cb6` | `QSG_NO_DEPTH_BUFFER=1` unless set (C27) | idle 392 MB (GPU 134), panels 849 (499), reload 470 (129) | idle 338 MB (GPU 80), panels 797 (446), reload 417 (77) |
| `e87229b` | the press filter on the shell windows instead of the application (C40) | every event of the process through `ShellHost::eventFilter` | only the shell windows' events; no measurable change in memory or start-up |
| `795d207` | one config-home walk for the snapshot and the watches at start (C36) | scan + watch 63-70 ms | 46-58 ms |
| not committed | `trimComponentCache()` one pass after `install()` (C31) | reloads plateau at 417-420 MB | 428-434 MB: no gain, so dropped |

### Before and after, whole lane

Release builds of `e34ce54` (before; the trace commit, otherwise `cdcc88a`) and `795d207` (after),
same deployment, same port copy. Memory from one run each through all phases (the runs above show
2-5 MB of run-to-run noise in private bytes); start-up from six interleaved starts each.

| Phase | Private MB before | after | GPU shared MB before | after | Threads before | after | Handles before | after |
|---|---|---|---|---|---|---|---|---|
| idle 30 s | 390 | 336 | 133 | 80 | 60 | 56 | 1551 | 1457 |
| launcher open | 482 | 431 | 209 | 155 | 65 | 63 | 1633 | 1610 |
| dashboard open | 850 | 797 | 498 | 446 | 68 | 65 | 3382 | 3358 |
| panels closed | 849 | 796 | 498 | 446 | 69 | 65 | 3390 | 3358 |
| reloads 1-10 | 469-473 | 416-421 | 129 | 77 | 64-68 | 64-68 | 3358-3409 | 3329-3395 |

| Start-up, six interleaved starts | before | after |
|---|---|---|
| reload started -> shell parts activated | 3098-3335 ms (median 3203) | 2082-2255 ms (median 2173) |
| process start -> first frame | 3364-4104 ms (median 3715) | 2325-2888 ms (median 2817) |

Hot reloads are unchanged: 3.1-3.7 s from `reload started` to the shell parts, of which
`clearSingletons()` is 2.5-2.8 s, and one or two in ten take 35 s (see the findings). They keep the
probe-engine check on purpose.

### Not done, and why

- `QSG_RENDER_LOOP=basic`: no memory gain on this machine (two threads fewer) and a risk to
  animation smoothness with several windows presenting on one thread.
- `setPersistentGraphics(false)` / `setPersistentSceneGraph(false)` on `ProxiedWindow`: they only
  act on a hidden window. The caelestia windows are never hidden (the panels live inside the one
  full-screen window), so nothing to measure here, and a popup that shows often would rebuild its
  device and pipelines on every show.
- `setDepthBufferFor2D(false)`: Qt documents that it keeps the depth-stencil buffers; the variable
  is what frees them.
- The plugin verdict cache (C36): the port has no plugin DLLs; the plugin walks take 9-11 ms.
- `trimComponentCache()` (C31): the private bytes are flat from the first reload on (no second
  generation of compiled QML stays resident since lane 0a flushes the deferred deletes before
  `clearComponentCache()`), and the trim measured nothing, so it was not committed.
- Deferring `watch()` past the first frame (C36): it would leave a short window in which an edit
  is missed; the walk now takes about 50 ms.

## Findings for the other lanes

- Reload stall, `host/quickshell/qml/Quickshell.qml` and `features/quickshell_io` (new finding,
  high): `clearSingletons()` takes 2.5-2.8 s per reload and 32-33 s in one or two of ten reloads.
  A GUI-thread stack taken 0.7 s into that step (`cdb -pv`, `~0kc`) sits in `SleepEx` under
  `QClipboard::text()` <- `QMimeData::text()` <- a QML binding evaluated in
  `QQmlComponentPrivate::completeCreate` of a singleton: the `Quickshell` singleton's
  `property string clipboardText: S.QuickshellSingleton.clipboardText` reads the system clipboard
  every time the singleton is created (each reload, and the first build), although the port only
  writes it. Qt's Windows clipboard retries `OleGetClipboard` with sleeps while another process
  holds the clipboard, and `GetClipboardData` waits up to 30 s for an owner that renders on
  demand: that matches the 32-33 s stalls. Fix: make `clipboardText` lazy (read on first access
  from QML, not in a binding at creation) or read it through `QClipboard::dataChanged` only when
  something observes it. Expected gain: about 2.5 s per reload, the 30 s stalls, and part of the
  first build's 0.9-1.5 s of creation. One stack is a strong lead, not proof: confirm with a
  `WAKU_TRACE` step around the read. The GlazeWM reconnect line before the slow reloads is
  probably timing, not the cause.
- COM thread mode, `app`/features: ten `Failed to initialize COM library (Cannot change thread mode
  after it is set.)` lines per reload: something calls `CoInitialize`/`OleInitialize` (STA) on a
  thread that is already MTA (C8 context).
- Handles after the dashboard (system_stats or media, lanes 1i/1k): the first dashboard open adds
  about 1,760 handles (1,600 -> 3,390), and a reload does not give them back (3,380-3,450 after ten
  reloads). Something process-wide (PDH GPU queries, C33, or a media or audio hub) keeps them.
- Retained after a reload: 470 MB private after a reload against 392 MB at a fresh start with the
  same shell (+80 MB, flat over ten reloads, so not a leak per reload). The heap keeps freed pages
  and something process-wide keeps what the dashboard loaded.
- Shell-side (the caelestia port, not this repository): GPU memory of layers. Unload panels when
  closed (`Loader { active: open }` or `LazyLoader`), and avoid `layer.enabled` on full-window items
  (`ContentWindow.qml:220`). With both panels opened once the shell holds 300-370 MB of textures
  that the software backend shows are not needed for the content itself.
- Wallpapers and thumbnails (lanes 1b, 1c): not measurable with `WAKU_WALLPAPER_DRYRUN=1` and no
  wallpaper change; the review's C28/C29 estimates stand. The production gap (982 MB against
  849 MB here with the panels opened) fits the real wallpaper, the tray and the thumbnails.
- Fonts: `--font-engine=directwrite`, which the port needs for its variable fonts, maps every
  installed font (about 100 MB of working set and 1,700 handles per `app/waku/font_engine.h`).
  Not measured separately here, since the port does not render with the GDI engine.

## Atlas copies (2026-11-01)

After the port's own fixes (the frame shadow layer only while a panel shows, the cover art rotation
from 0, the panels unloaded when closed) the shell still kept about 400 MB of GPU memory once the
launcher had been opened, and that block did not move when the panels closed. Its cause is the
texture atlas of the scene graph.

Method: the deployed engine (`4a19cc6`), the port's `main` (`80fc100`), the private set-up above;
phases as before, 5 s per open panel. Committed `MEM_PRIVATE` regions from `!address` (`cdb -pv`)
before and after one launcher open and close, diffed by size.

| Run | New private regions after the launcher |
|---|---|
| default | 11 regions of 32.0 MB (352 MB) plus 22, 17, 16 and 14 MB ones; 438 MB in all |
| `QSG_ATLAS_WIDTH=1024`, `QSG_ATLAS_HEIGHT=1024` | 59 regions of 4.0 MB (236 MB), two of 32 MB; 410 MB in all |
| `QSG_ATLAS_SIZE_LIMIT=1` (no atlas) | one of 32 MB, two of 21-22 MB, two of 16-17 MB; 145 MB in all |

The block follows the atlas size: Qt gives the main window one atlas texture of 4096x2048 BGRA
(32 MB; `rhi texture atlas dimensions: 4096x2048` in `QSG_INFO`) and the four small windows
512x512. The atlas does not grow and Qt keeps one per window, so the 9-11 extra atlas-sized
regions are the D3D11 driver's: it holds copies of the atlas after a burst of uploads into it (the
launcher's app icons at 100x100, each one an upload into the atlas) and keeps them. Neither the
shell icon extraction (no GPU memory in a test process that reads all 212 icons of
`shell:AppsFolder`), the distance-field glyph caches (`QML_DISABLE_DISTANCEFIELD=1`: no change)
nor the frame shadow layer (`layer.enabled: false`: 30 MB less) is it.

GPU shared MB, deployed engine, one run each:

| Phase | default | no atlas |
|---|---|---|
| idle | 50 | 49 |
| launcher open (5 s) | 125 | 158 |
| launcher closed | 445 | 158 |
| dashboard opened and closed | 445 | 158 |
| all closed, 13 s later | 445 | 192 |
| dashboard first: idle, dashboard opened and closed | 50 | 49 |
| then launcher opened and closed, 13 s later | 514 | 228 |

The jump comes within a second of the launcher's first frames with its icons, on the open or on
the close depending on when the icons arrive; it does not grow on later opens. The dashboard alone
adds nothing on this machine. Private bytes follow the GPU numbers (894 -> 608 MB with both panels
opened, dashboard first).

Change: `waku.exe` sets `QSG_ATLAS_SIZE_LIMIT=1` unless the caller set it, so every image gets its
own texture. A debug build of the change, without the variable set by the caller, holds 158 MB
after the launcher opened and closed (default: 446 MB). Rendering is the same; the cost is one batch per image instead of one per atlas, which
the shells' few dozen visible images do not feel.

What is left after the change (about 110-180 MB over idle with both panels opened): two full-window
layers (21-22 MB each: the frame shadow layer and its `MultiEffect`), one 32 MB and two 16-17 MB
regions not identified yet, and the icon textures themselves.
