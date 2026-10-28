# Qt Quick bar: how layout and widget config are declared

Status: proposal, for the `spike/qt-quick-bar` branch. Nothing here is implemented beyond the
spike (`spike/`): a QML bar window, the `barnacle.services` module and hot-reloaded user widgets.

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

Config home (default `%USERPROFILE%\.config\barnacle\`, overridable with `--config-home`):

```
bar.qml          entry point: declares the bars
theme.qml        optional: colours, fonts, radii (see Theme)
widgets/         user widgets, one component per file
  Clock.qml
  Pill.qml       shared building blocks
```

`widgets/` is a normal QML directory import, so `Clock.qml` is usable as `Clock {}` after
`import "widgets"`. There is no registry and no loader list: a widget exists because a file
exists. Built-in widgets ship in the `barnacle.widgets` module with the same shape, so a user widget
can copy or wrap one.

## bar.qml

```qml
import barnacle
import barnacle.widgets
import "widgets"

Config {
    Bar {
        screens: ["primary"]          // "primary", "all", or device names
        position: Bar.Top
        height: 40
        reserveSpace: true            // register as an app bar and reserve the work area
        backdrop: Bar.Blur            // Bar.None | Bar.Blur | Bar.Mica

        layout: BarLayout {           // instantiated once per matching screen
            left:   [ Workspaces {}, ActiveWindow {} ]
            center: [ Clock { format: "ddd d MMM  HH:mm" } ]
            right:  [
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
- `BarLayout` is an `Item` with three list properties, `left`, `center` and `right`. A widget
  that sets `visible: false` leaves the layout (the spike already does this).
- Widget options are ordinary QML properties and signal handlers:
  `Clock { format: "HH:mm"; onClicked: Shell.run("calc.exe") }`.

## Widget contract

A widget is any QML `Item` with an `implicitWidth` and `implicitHeight`. Conventions, not rules:

- Take data from `barnacle.services` (`Memory`, `Cpu`, `Battery`, `AudioOutput`, `Media`, `Komorebi`,
  `GlazeWM`, `Process`). Polled sources are instantiated by the widget; shared event sources are
  singletons.
- Expose options as `property` declarations with defaults. Document them in a comment at the top
  of the file.
- Use `Theme` for colours and fonts so a theme change restyles every widget.
- Do not block: long work goes through `Process` or a service.

## Theme

`theme.qml` is an object with plain properties (`accent`, `surface`, `text`, `fontFamily`,
`radius`, ...). The host loads it into a `Theme` singleton before any bar is built, so widgets
read `Theme.accent`. Missing properties fall back to built-in defaults. This replaces CSS for
colours and fonts; per-widget styling is a property on that widget.

## Errors and reload

- The host watches the config home. On any change it re-evaluates `bar.qml` in a new engine
  context and swaps the bars only when that succeeds. A syntax or type error keeps the previous
  bars and shows a `ConfigError` pill (and logs the file, line and message) instead of leaving
  the desktop without a bar.
- A single broken widget file fails only that widget: its `Loader` shows an error placeholder.
- `qmllint` works on the config home, so editors give completion and diagnostics for free.
- `--safe` starts with the built-in layout and ignores the config home.
- `yasbc reload` forces a reload.

## Trust and safety

Config is code. A widget can run any program through `Process` and read any file, with the bar's
privileges. That is acceptable for a single-user tool. If widgets are ever shared, add an
explicit trust prompt per file; a QML sandbox is not practical.

## Versioning

`import barnacle 1.0` and `import barnacle.services 1.0` are the public API. Adding properties is
compatible; removing or renaming one is a major bump with the old name kept for one release.

## Delivery order

1. `Config`, `Bar`, `BarLayout` in a `barnacle` QML module; the host builds one window per screen from
   them; replace the spike's "load every file in `widgets/`" with the explicit layout. (Medium.)
2. Built-in `barnacle.widgets`: Clock, Cpu, Memory, Battery, Volume, Media, Workspaces (komorebi or
   GlazeWM, chosen by what is running). (Medium.)
3. Transactional reload with the error pill, `--safe`, `yasbc reload`. (Small.)
4. `Theme` singleton and `theme.qml`. (Small.)
5. Popups (`PopupWindow`) and tooltips, then systray and taskbar, which need native-window work.
   (Large.)

## Decisions taken

- This is a new application with its own name; it is not a YASB fork and reads no YASB config.
  The name is barnacle (see [Name](#name)), always written in lower case.
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

barnacle. It clings to the edge of the screen. The official name is lower case, everywhere:
the executable is `barnacle.exe`, the CLI is `barnacle reload`, the config home is
`%USERPROFILE%\.config\barnacle\`, and the QML modules are `barnacle`, `barnacle.widgets` and
`barnacle.services`.

It was checked only for collisions on GitHub (no software of that name among the top results; no
trademark or domain search yet). fascia, transom, overbar and others were considered. The
repository, the `yasb_*` targets and the `yasb::` C++ namespaces outside `spike/` still carry the
old name; renaming them is a separate step.
