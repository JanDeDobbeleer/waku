# Qt Quick bar: how layout and widget config are declared

Status: proposal, for the `spike/qt-quick-bar` branch. Nothing here is implemented beyond the
spike (`spike/`): a QML bar window, the `tidy.services` module and hot-reloaded user widgets.

## Decision

QML is the configuration format. One file type describes the bars, their layout, each widget's
options and the widgets themselves. There is no YAML, no CSS and no schema file.

Reasons:

- A user widget is already a `.qml` file (the spike proves this, with hot reload). If layout and
  options lived in a second format, every widget option would need a schema entry, a validator
  and a bridge into QML. QML properties already are typed options with defaults.
- Layout that is code can compute: show a widget only on one monitor, build a list from a loop,
  switch on the time of day.
- The project is single-user and greenfield, so a Python-YASB-compatible config is not a goal.

Rejected: YAML layout plus QML widgets (two formats, a schema that duplicates QML property
declarations); JSON or TOML layout (cannot express conditions; fine for a GUI editor, which could
generate QML later); a C++ builder API (no hot reload, no sharing).

## Files

Config home (default `%USERPROFILE%\.config\tidy\`, overridable with `--config-home`):

```
bar.qml          entry point: declares the bars
theme.qml        optional: colours, fonts, radii (see Theme)
widgets/         user widgets, one component per file
  Clock.qml
  Pill.qml       shared building blocks
```

`widgets/` is a normal QML directory import, so `Clock.qml` is usable as `Clock {}` after
`import "widgets"`. There is no registry and no loader list: a widget exists because a file
exists. Built-in widgets ship in the `tidy.widgets` module with the same shape, so a user widget
can copy or wrap one.

## bar.qml

```qml
import tidy
import tidy.widgets
import "widgets"

Config {
    Bar {
        screens: ["primary"]          // "primary", "all", or device names
        position: Bar.Top
        height: 40
        reserveSpace: true            // register as an app bar and reserve the work area
        backdrop: Bar.Blur            // Bar.None | Bar.Blur | Bar.Mica

        layout: BarLayout {           // instantiated once per matching screen
            start:  [ Workspaces {}, ActiveWindow {} ]
            center: [ Clock { format: "ddd d MMM  HH:mm" } ]
            end:    [
                Media {},
                Volume { step: 5 },
                Battery { visible: Battery.available },
                Memory {}, Cpu {}
            ]
        }
    }
}
```

- `Config` is the root. It holds `Bar` objects and an optional `theme`.
- `Bar` is a declaration, not a window. For each matching screen the host creates one frameless
  window and builds `layout` inside it. `Window.window.monitorHandle` and `screen` are available
  to anything inside.
- `BarLayout` is an `Item` with three list properties, `start`, `center` and `end`. A widget
  that sets `visible: false` leaves the layout (the spike already does this).
- Widget options are ordinary QML properties and signal handlers:
  `Clock { format: "HH:mm"; onClicked: Shell.run("calc.exe") }`.

## Widget contract

A widget is any QML `Item` with an `implicitWidth` and `implicitHeight`. Conventions, not rules:

- Take data from `tidy.services` (`Memory`, `Cpu`, `Battery`, `AudioOutput`, `Media`, `Komorebi`,
  `GlazeWM`, `Process`). Polled sources are instantiated by the widget; shared event sources are
  singletons.
- Expose options as `property` declarations with defaults. Document them in a comment at the top
  of the file.
- Use `Theme` for colours and fonts so a theme change restyles every widget.
- Do not block: long work goes through `Process` or a service.

### Built-in widgets (`tidy.widgets`)

Plain QML on top of `tidy.services`; a user can copy one into `widgets/` and change it.

| Widget | Shows | Options |
|---|---|---|
| `Pill` | the building block: rounded background, optional text, extra items | `text`, `fill`, `hoverFill`, `textColor`, `fontSize`, `padding`, `interactive`; signals `clicked`, `rightClicked`, `scrolled(delta)` |
| `Clock` | date and time | `format` (Qt date-time format); redraws each minute, or each second if the format has seconds |
| `Cpu` | processor load | `format` (`%1` percent), `interval`, `warnAbove`, `warnFill` |
| `Memory` | memory use | `format` (`%1` percent, `%2` used GiB, `%3` total GiB), `interval`, `warnAbove`, `warnFill` |
| `Battery` | charge; hidden without a battery | `format`, `chargingFormat` (`%1` percent), `interval`, `lowBelow`, `lowFill`, `chargingFill` |
| `Volume` | output volume; wheel changes it, click mutes, right click opens the sound settings | `step`, `format` (`%1` percent), `mutedFormat` |
| `Media` | now playing with album art; click plays or pauses, right click skips, wheel steps; hidden without a player | `maxWidth`, `showArt`, `playingGlyph`, `pausedGlyph` |
| `Workspaces` | this monitor's workspaces from komorebi or GlazeWM | `hideEmpty`, `scrollable`, `pillWidth`, the four fills |

Rules the set follows:

- A widget is a `Pill` (or a row of them) so that spacing, height (24 px) and the hover highlight
  are the same everywhere. Colours are properties with defaults today; the `Theme` singleton will
  feed those defaults later without changing any config.
- Text options are format strings with `%1`, `%2`, `%3` placeholders, not a template language.
- A widget that has nothing to show hides itself with `visible: false` and leaves the layout.
- The widgets share names with services (`Cpu`, `Memory`, `Battery`, `Media`, `Workspaces`), so the
  module imports the services as `import tidy.services as S` and writes `S.Cpu {}`. A user
  config imports `tidy.widgets`; a custom widget that needs services imports
  `tidy.services` and avoids those five names, or imports it `as S` too.

## Theme

`theme.qml` is optional. Its root is a `ThemeSpec` with plain properties; the host copies them into
the `Theme` singleton before it builds the bars, so widgets read `Theme.accent` and so on. A
property the file does not name keeps its default, and removing the file (or a line) restores the
default on the next reload. This replaces CSS for colours and fonts; per-widget styling is a
property on that widget.

```qml
import tidy
ThemeSpec { accent: "#ff8800"; fontFamily: "Cascadia Code"; radius: 6 }
```

`spike/config/theme.qml` lists every property with its default.

| Property | Default | Used for |
|---|---|---|
| `text`, `surface` | `#ffffff`, `#66101018` | text; the bar background (a `Bar` can set its own `background`) |
| `accent`, `warn`, `good`, `caution` | `#5b8def`, `#e5484d`, `#38c172`, `#f0c36d` | focus; high load, low battery, errors; charging; a mode |
| `fill`, `fillHover`, `fillActive`, `fillUsed`, `fillEmpty` | white at 13%, 27%, 33%, 20%, 9% | a widget's background: normal, hovered, shown, holding something, empty |
| `popupSurface`, `popupBorder` | `#f0181820`, white at 20% | popups and tooltips: nearly opaque, because they sit over other windows |
| `accentFill`, `warnFill`, `goodFill`, `cautionFill` | read only | the colour above at a translucent alpha, for a widget background |
| `fontFamily` | the system font | all text |
| `fontSize`, `smallFontSize` | 13, 12 | text; workspace labels, error pill |
| `pillHeight` | 24 | height of a `Pill` and of a workspace button |
| `radius` | -1 | corner radius in pixels; negative is fully rounded, a large value is clamped to a full pill |
| `padding`, `spacing` | 10, 12 | inside a widget; between widgets in a `BarLayout` |

Design notes:

- `Theme` and `ThemeSpec` share one base class that holds the properties, so a name exists once.
  The host copies every writable property from the spec, which is why a removed line resets.
- The theme is applied after the bars are checked and before they are built, and widgets bind to
  it, so a change to the singleton also restyles live bars without a rebuild (the tests do this).
- A broken `theme.qml` is a failed reload like a broken `bar.qml`: the previous bars and the
  previous theme stay and the error pill says `theme.qml:LINE:COL: ...`.
- Not in the theme yet: per-widget colours of custom widgets (they read `Theme.*` themselves),
  light and dark variants, and named theme files to switch between.

## Popups and tooltips

Two types in the `tidy` module; both are plain QML and both are windows of their own, because
the bar window is only as tall as the bar.

```qml
Pill {
    id: pill
    text: "Volume"
    tooltip: "hover text"              // a Pill has this built in
    onClicked: popup.toggle()

    PopupWindow {
        id: popup
        anchorItem: pill
        Text { text: "anything"; color: Theme.text }
    }
}
```

- **`PopupWindow`**: `anchorItem`, `open()`, `close()`, `toggle()`, `opened` and `closed`. What is
  declared inside is stacked in a column and sizes the window; `padding`, `spacing`, `gap` and
  `margin` tune it. It opens just outside the bar (below a top bar, above a bottom bar), centred
  on the widget.
- **`Tooltip`**: `text`, `delay` (600 ms). Declared as a direct child of the item to watch (not
  inside a Row or Column, because it fills its parent). Shown after the delay, hidden when the
  pointer leaves or a button is pressed. It never takes focus or input.
- **Placement** is a pure function (`PopupPlacement.place`, with tests): centre on the anchor,
  slide sideways to stay `margin` inside the screen, flip to the other side when the preferred
  side has no room, clamp when neither fits. The anchor is the widget's width and the bar's
  height, so a popup never overlaps the bar.
- **Look**: both take `Theme.popupSurface`, `Theme.popupBorder`, the radius and the font, and get
  the bar's backdrop (blur or mica) through `BarHost.decorate`, so they match the bar.
- **Dismissal** is explicit, not `Qt.Popup`: measured on Windows, a `Qt.Popup` window did not close
  on a click elsewhere. A popup closes on Escape, on a press in any bar window (the host watches
  presses with an application event filter and emits `barPressed`, because bar windows never take
  activation), and when it loses activation (a click in another application). A press that
  closes a popup on its own widget does not reopen it: `toggle()` ignores a call within 250 ms of
  closing.
- **Built-in widgets using them**: `Clock` (calendar, full date), `Volume` (slider, mute, output
  devices, current device), `Media` (art, track, seek bar, controls, track), `Cpu`, `Memory`,
  `Battery` (tooltips with per-core load, memory and swap, charge and time left). The calendar
  and the slider are public widgets too (`Calendar`, `Slider`).
- **Not done**: keyboard navigation inside a popup, a popup that stays open while another opens
  (one opens, the press that opens the next closes it), and anchoring to a screen edge instead of
  a widget (for example a control centre).

## Errors and reload

- The host watches the config home (every `*.qml` file and folder under it, debounced). On a
  change it first checks `bar.qml` in a throwaway engine: it must compile (so every widget file it
  imports compiles too), every type must exist and the root must be a `Config`. Creating the
  config there builds no widgets and starts no service, because a `Bar`'s `layout` is a lazy
  component. Only a config that passes replaces the current bars.
- A config that fails the check keeps the previous bars. The first error (`bar.qml:32:1: ...`)
  shows as a pill in them and the full list goes to the log. Fixing the file clears the pill.
- If the very first load fails there are no bars to keep, so a built-in fallback bar shows the
  error. The same happens in the rare case that the check passes but building the windows fails.
- Why a throwaway engine and not "build the new bars next to the old ones": Qt reloads a changed
  file only when no object made from the old components is alive (`clearComponentCache()` drops
  unreferenced components only). Measured in the spike: a directory-imported widget file kept its
  old content while the old bars were alive. So the old bars are torn down after the check, not
  before it.
- A broken widget file is a broken config (the whole reload is rejected), not one broken widget:
  widgets are instantiated by the layout, so a missing or invalid widget type fails the compile.
  Errors that only appear at run time inside a widget are logged by the QML engine and do not stop
  the bar.
- The QML disk cache is off: a config is edited by hand and a cached compilation must never
  outlive an edit.
- `qmllint` works on the config home, so editors give completion and diagnostics for free.
- `--safe`: see below.
- `tidyc reload`: see below.

### Safe mode and the command line

- **`tidy.exe --safe`** starts the built-in bar (`tidy.widgets/SafeConfig.qml`: Clock,
  Workspaces, Volume, Battery, Cpu, Memory) with the default theme. Nothing in the config home is
  read, so a broken `bar.qml`, a broken `theme.qml` or a missing folder cannot get in the way, and
  nothing is watched. It is the way back to a bar when the config is wrong: fix the files, then
  start normally. `tidyc reload` in safe mode rebuilds the same bar.
- **`tidyc.exe`** is a separate console program, as `komorebic` is to komorebi: the bar is a
  Windows (GUI) program with no console to print to or exit code to return. It talks to the
  running bar over a named pipe:

  | Command | Answer | Exit code |
  |---|---|---|
  | `tidyc reload` | `ok`, or `error: bar.qml:12:5: ...` (the previous bars stay) | 0, or 1 |
  | `tidyc status` | `running; bars=1; safe=no; error=none` | 0 |
  | `tidyc quit` | `ok`, then the bar exits | 0 |
  | no bar running | `tidyc: no bar is running for <config home>` | 2 |
  | bad command, no answer | usage or the reason | 3 |

- The instance is named by the config home: `--config-home <dir>` or `$TIDY_CONFIG_HOME` on
  both programs picks it, and the pipe name is a short hash of the folder (case and trailing
  slashes do not matter), so two bars with different config homes do not meet.
- One bar per config home: a named mutex held by the running bar refuses a second start with
  `tidy is already running for <config home> (tidyc quit stops it)` and exit code 1. (Two
  `QLocalServer`s can listen on one pipe name on Windows, so the pipe alone cannot tell.)
- Protocol: one line in, one line out, then the connection closes. A line over 1 KiB is refused
  and a client that stalls is dropped after 3 s, so a stray process cannot hold the bar.

## Trust and safety

Config is code. A widget can run any program through `Process` and read any file, with the bar's
privileges. That is acceptable for a single-user tool. If widgets are ever shared, add an
explicit trust prompt per file; a QML sandbox is not practical.

## Versioning

`import tidy 1.0` and `import tidy.services 1.0` are the public API. Adding properties is
compatible; removing or renaming one is a major bump with the old name kept for one release.

## Delivery order

1. `Config`, `Bar`, `BarLayout` in a `tidy` QML module; the host builds one window per screen from
   them; replace the spike's "load every file in `widgets/`" with the explicit layout. (Medium.)
2. Built-in `tidy.widgets`: Clock, Cpu, Memory, Battery, Volume, Media, Workspaces (komorebi or
   GlazeWM, chosen by what is running). (Medium.)
3. Transactional reload with the error pill, `--safe`, `tidyc reload`. (Small; done.)
4. `Theme` singleton and `theme.qml`. (Small.)
5. Popups (`PopupWindow`) and tooltips, then systray and taskbar, which need native-window work.
   (Large.)

## Decisions taken

- This is a new application with its own name; it is not a YASB fork and reads no YASB config.
  The name is tidy (see [Name](#name)), always written in lower case.
- `Workspaces` is one shared model over komorebi and GlazeWM (below).
- `Shell` is a small helper in the services module (below).

### Shared `Workspaces` model

One singleton, `Workspaces`, hides the window manager. Widgets never mention komorebi or GlazeWM.

```
Workspaces.provider     "komorebi" | "glazewm" | ""        which one is connected
Workspaces.connected    bool
Workspaces.monitors     [{ handle, name, workspaces: [
                           { id, label, focused, active, hasWindows, windowCount } ] }]
Workspaces.focus(monitorHandle, workspaceId)
Workspaces.focusNext(monitorHandle) / focusPrevious(monitorHandle)
```

- `active`: the workspace shown on its monitor. `focused`: active on the focused monitor.
  komorebi gives the first directly (per-monitor focused index); GlazeWM gives `isDisplayed` and a
  global focus flag. Both map onto the pair without loss.
- `id` is opaque to widgets. For komorebi it is the workspace index, for GlazeWM the workspace
  name; `focus()` translates it (komorebi also needs the monitor index, found from `handle`).
- `label` is the workspace name, or its 1-based index when komorebi's name is empty.
- When both are connected, `Workspaces.prefer` (a string property, default `"komorebi"`) decides.
- Things that exist in only one manager (GlazeWM binding modes and tiling direction, komorebi
  layouts) stay available on `GlazeWM` and `Komorebi`; the shared model is the common subset,
  not a lowest common denominator that hides them.
- The existing `GlazeWM` and `Komorebi` singletons become the backends and keep their tests;
  `Workspaces` is added on top with its own tests against fakes of both.

### `Shell` helper

A singleton for the things a widget does on click, so that most widgets need no `Process`:

```
Shell.start(program, args)     detached, no console window, nothing returned
Shell.command(commandLine)     `cmd /c <commandLine>`, hidden, detached
Shell.open(target)             ShellExecute "open": URL, file or folder
Shell.homeDir / Shell.configHome / Shell.env(name)
```

`Process` stays for commands whose output a widget displays. `Shell` reuses the platform helpers
that already exist (`launchHiddenShell`, `launchKomorebic`, `shell_open`). Notifications and power
actions (lock, sleep) are not part of the first cut.

## Name

tidy. A bar keeps the desktop tidy: one thin strip with everything in its place. The official
name is lower case, everywhere: the executable is `tidy.exe`, the command-line client is
`tidyc.exe` (`tidyc reload`; a console program, so it is separate from the GUI executable), the
config home is `%USERPROFILE%\.config\tidy\`, and the QML modules are `tidy`, `tidy.widgets` and
`tidy.services`. The environment variable is `TIDY_CONFIG_HOME` and the control pipe is named
`tidy-<hash of the config home>`.

History: the working name was barnacle until this step; fascia, transom, overbar and others were
considered before it. The spike was renamed in one commit (modules, C++ namespace, targets,
executables, pipe, environment variable and these docs). The repository, the `yasb_*` targets and
the `yasb::` C++ namespaces outside `spike/` still carry the old name; renaming them is a separate
step.

Collisions, checked only on GitHub (no trademark or domain search yet): no Windows bar is called
tidy. The word is also HTML Tidy (`htacg/tidy-html5`), a widely installed command-line program whose
executable is `tidy.exe`; on a machine that has it on PATH the two would clash. Neither is a
blocker (the PATH order decides, and the executable could ship as `tidybar.exe` while the product
stays tidy), but it should be decided before anything is published or installed.
