---
name: waku-shell
description: Create, port or restyle a waku shell (bar, dock, launcher, panels) so that every element works. Use when the user wants a waku config or shell, wants to recreate a look from a screenshot or from a Reddit post such as an r/unixporn URL, wants to port a Quickshell or Hyprland shell to Windows, or wants to change an existing waku shell.
---

# waku shell authoring

waku is a Qt Quick shell runtime for Windows, configured in QML, with Quickshell-compatible
modules. waku is in active development: flags, QML types and formats can change without
notice, so read `reference/engine-surface.md` for the current surface. A shell is a config home: a folder with `shell.qml` and its own QML. The engine holds only
what QML cannot do. This skill gets a shell right the first time.

## Hard rules

1. **Everything works.** No element ships without a real backend. No mock data, no empty
   `onClicked`, no placeholder panel, no widget bound to an empty model, no hardcoded value that
   stands in for live data. If an element cannot work on Windows, drop it from the shell and tell
   the user. A visual match never outranks function.
2. **Say what you verified.** Use the three levels in `reference/verify.md`. Never claim a level you
   did not reach. You cannot press keys or answer UAC; mark those rows `needs the user`.
3. **Never touch the user's live setup.** Build and test in a throwaway config home with
   `--no-tray --no-app-bar --keep-taskbar --no-keys --no-tiling` (waku reserves screen space, hides
   the taskbar, starts the key daemon and tiles windows by default). Do not write to
   `%USERPROFILE%\.config\waku`. Start the live bar only when asked.
4. **Licences.** Copy no code from a shell under a licence that is not MIT into this repository.
   From a third-party config, take the look (colours, sizes, layout) and write the QML yourself.
5. **No shell-specific engine code.** A missing engine feature is reported as a generic gap. The
   element stays dropped until the engine has it.

## Workflow

Do the steps in order. Do not start writing QML before step 3 is written down.

### 1. Intake

- Identify the source: screenshot(s), a Reddit post URL, a path to an existing shell, or a
  description. See `reference/from-image.md` for screenshots and Reddit.
- Confirm the target: which config home (a new folder unless the user names one), which monitor,
  its resolution and scale. Read the target folder first if it exists. Do not assume a shell name
  or path; check it exists.
- Ask a question only when the source cannot answer it and a wrong guess costs a rebuild.

### 2. Spec the look

Write measured values, not adjectives. "Flush", "tight" and "rounded" are not specs. Output:

- Tokens: colours (hex with alpha), fonts and sizes, radii, bar thickness, gaps, padding, border
  widths, opacity, blur or not.
- Layout: edges used, what sits in each region, order, spacing.
- Mark each value `measured` or `guessed`. Tell the user which values are guessed.

### 3. Build the ledger

One row per visible element (widget, button, indicator, panel, popup, menu entry, shortcut):

| Element | Should do | Backend on waku | Status |
|---|---|---|---|

Status is one of:

- `functional`: a built-in widget, a `waku.services` service or a Quickshell module does it.
- `functional-with-difference`: works, behaves differently. Say how.
- `not possible`: no backend. **Drop it.** Name the Windows reason and any real substitute.
- `engine gap`: waku could do it, does not yet. Drop it. Describe the generic engine feature.

Find backends in `reference/engine-surface.md`. Linux-only features and the replacements to use are
in `reference/porting-checklist.md`. Rows with status `not possible` or `engine gap` do not enter
the shell. Do not build a lookalike.

### 4. Build

- Work in a new config home. Start from `examples/config` (a bar) or the closest example
  (`examples/sidebar`, `examples/launcher`, `examples/gallery`). Run `wakuc config init` for the
  built-in starter.
- Prefer built-in widgets from `waku.widgets` and the theme (`theme.qml`, `ThemeSpec`). Copy a
  built-in into `widgets/` under a new name to change it.
- Own types: one component per file in the config home. Do not name a file after a waku service
  (`Wallpapers.qml`, `Workspaces.qml`, `Bluetooth.qml`, `Media.qml`): `import Quickshell` shows
  Quickshell names only, so your file is your own type.
- Read `reference/windows-gotchas.md` before writing layout, keys or launch code.
- Services are `null` or unavailable until they report ready. Never gate a widget's `visible` on
  such an object.
- Workspace ids are strings (`DISPLAY1:0`). Use `var`, not `int`.
- Add what the shell needs to run beyond QML: a `keys.conf` beside `shell.qml` (waku starts
  `wakukeys.exe` with it) and, if the shell needs an environment, a `start.ps1`. Window gaps, layout and
  ratio come from `Tiling` (`gap`, `ratio`, `layout`), not from a separate tiler config.

### 5. Audit and verify

- Run `scripts/audit.ps1 -ConfigHome <dir>`. It flags stubs, mock text and Linux-only calls. Fix
  every finding or drop the element. Exit code 0 is required.
- Run `scripts/run-candidate.ps1 -ConfigHome <dir>` for level 1 (loads, `error=none`).
- Run it on the desktop (`-Visible`) and capture with `scripts/capture.ps1` for level 2. Compare
  crops with the reference side by side.
- Exercise each interactive row for level 3 (see `reference/verify.md`).

### 6. Deliver

Report, in this order:

1. How to run it (exact command, throwaway first, then how to make it live and start at logon).
2. The ledger with final status per row and the verification level reached.
3. **Dropped elements**: what, why, and any real substitute.
4. Guessed values, `needs the user` checks, and engine gaps.

Do not commit or publish unless asked. If the shell is meant for the waku-ports repository, say
which licence applies.

## Reference

- `reference/engine-surface.md`: modules, types, widgets, theme, CLI.
- `reference/windows-gotchas.md`: lessons from past port work.
- `reference/porting-checklist.md`: Hyprland, Wayland and Linux replacements.
- `reference/verify.md`: the three levels, capture, and the report.
- `reference/from-image.md`: screenshots and Reddit.
- `scripts/`: `audit.ps1`, `run-candidate.ps1`, `capture.ps1`. Run them with `pwsh`.
