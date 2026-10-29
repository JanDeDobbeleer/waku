# wabar styling: why a look change needs a rebuild, and how to stop that

Status: proposal, 2026-10-29. Nothing here is implemented. Branch `restructure/wabar` at `5916d03`.

The owner's problem statement: "The gradient coloring change required a change in the embedded QML
and a rebuild. That should never be the case. Ideally the styling lives outside of the engine and
can be adjusted without rebuilding."

Short answer: the rebuild happened because the island fill was a single colour in engine code, not
because the QML is embedded. Loading the built-ins from disk (option O1) removes the rebuild for the
owner but still means editing engine files. What removes the cause is a theme that can hold rich
values (paints, per-widget style bundles, replaceable backgrounds), and built-in widgets that read
only that theme. Sections 3 and 4 propose that; section 5 orders the work.

## 1. Root cause

### 1.1 What forces a rebuild today

Four things stack up. Any one of them alone forces a rebuild for some kind of change.

| # | Mechanism | Where | Effect |
|---|---|---|---|
| R1 | The three modules are embedded | `src/host/CMakeLists.txt` (`wabar`), `src/modules/qml_modules.cmake` (`wabar.widgets`, `wabar.services`): `qt_add_qml_module` static libraries, QML files as resources under `qrc:/qt/qml/wabar/...` | The engine loads every built-in `.qml` from the executable. Editing `src/features/*/qml/*.qml` has no effect until `wabar.exe` is rebuilt. |
| R2 | The generated `qmldir` pins the resource copy | `build/<preset>/src/modules/widgets/qmldir` contains `prefer :/qt/qml/wabar/widgets/` | Even a copy of the module on the import path loads its files from the resources. `deploy` copies this `qmldir` to `dist/wabar/modules/`, so that tree cannot override anything either (it is not on the import path anyway). |
| R3 | The `Theme` token set is closed C++ | `src/host/qml/bar_theme.h`: 37 writable `Q_PROPERTY`s plus 4 derived ones on `ThemeValues`; `assignFrom` copies only those | A new token (a new kind of knob) is a C++ change. `bar_theme.h` changed in 5 of the 6 visual commits so far (`502bf32`, `2378fca`, `6881a62`, `5fd2196`, `dd3b0c2`). Each token is also a single scalar type (`QColor`, `int`, `QString`): a colour cannot become a gradient. |
| R4 | Structural variants exist only as code | `BarLayout.qml` (island shape and fill), `Workspaces.qml:20` (`style: "names" \| "dots"`), `Pill.qml:40` (`flat` decides bar or popup look), per-widget `warnFill`, `lowFill` | A new variation of an existing part needs new QML in the engine, plus a new property to switch it on. |

R1 and R2 decide *where* the engine code lives. R3 and R4 decide *how much of the look is engine
code at all*. Removing R1/R2 alone turns "rebuild" into "edit an engine file and reload", which is
better for the owner's loop but still not "styling lives outside the engine".

The user config is already outside: `bar.qml`, `theme.qml` and `widgets/*.qml` load from disk with
`QML_DISABLE_DISK_CACHE=1` (`src/app/wabar/main.cpp:20`), the host watches the config home, checks
the change in a throwaway engine and rebuilds the bars (`src/host/qml/bar_host.cpp:202`). That
pipeline works and is tested (`src/host/tests/test_theme.cpp`, 28 tests).

### 1.2 Three classes of visual change

| Class | Definition | Who does it | Rebuild today |
|---|---|---|---|
| A | A new value for an existing token: a colour, a size, a font | user, in `theme.qml` | no, if the token exists and every built-in honours it |
| B | A new variation of an existing structure: a gradient instead of a flat fill, a different look per island or per widget, a state variant | should be the user, through a hook the engine exposes | yes: no hook exists, so the variation is written into the engine |
| C | New structure or behaviour: a new widget, a new data source, a new interaction, a new layout model | engine developer | yes, legitimately |

### 1.3 The gradient was class B

Commit `dddcf71` ("paint the bar islands with a gradient") added `startFill`, `centerFill` and
`endFill` to `BarLayout` and gradient painting to the island `Canvas` (`src/host/qml/BarLayout.qml`
lines 37-61 and 125-133), plus 111 lines of tests. The island already existed (class C work done in
`6881a62`). What did not exist was any way to say *how* it is painted: the fill was
`fillColor`, a single `color` taken from `Bar.background` or `Theme.surface`
(`BarLayout.qml:73-75`), and the canvas did `ctx.fillStyle = island.fillColor`. A colour-typed token
cannot hold a gradient, so the variation had to become code.

What would have made it class A: an island fill token typed as a *paint* (a colour, a list of
colours, or a gradient description) instead of a colour. What would have made it a user-side change
without any token: a replaceable island background (a `Component` hook), so a user draws the island
body however they like while the engine keeps the shape and the placement.

Python YASB, for comparison: the island shape is code there too (`D:/yasb/src/core/bar_style.py`,
`AdaptiveBarFrame`, 406 lines), but the frame does not choose a colour. Qt's stylesheet paints the
whole `.yasb-bar` background, and `paintEvent` then cuts the gaps between islands out of it
(`bar_style.py:211-217`). The engine owns the shape; the stylesheet owns the paint. A
`background: qlineargradient(...)` on `.yasb-bar` would in principle have produced gradient islands
with no code change (not tested). Engine knobs that are not paint (`islands`, `islandradius`,
`grouppadding`) are `pyqtProperty`s set from CSS with `-qproperty-` (`~/.config/yasb/styles.css`
lines 52-58). That split, engine-owned shape and data-owned paint, is the lesson for wabar.

### 1.4 Audit: are the class A tokens exposed and honoured?

Exposed: yes. Every writable `ThemeValues` property is a `Q_PROPERTY` and `assignFrom` copies them
all through the meta-object (`src/host/qml/bar_theme.cpp:12`), so any of the 37 can be set from
`theme.qml`. Honoured: only partly. Counts are over `src/host/qml`, `src/ui` and
`src/features/*/qml` (31 files, 5,334 lines).

| Kind | Hard-coded sites | Through `Theme` | Where (file:line) |
|---|---|---|---|
| Colours outside the palette | 13 | - | `BarWindow.qml:52` "white"; `GitHub.qml:39` #ef4747, `:109` #949cbb, `:273` rgba(35,38,52,0.2), `:354` #737994; `Media.qml:151` #51576d, `:186` #babbf1; `Memory.qml:78` #ca9ee6; `WallpaperGallery.qml:21` #ffffff, `:88` #000000, `:130` #ffffff, `:168` rgba(1,1,1,0.6); `Wallpapers.qml:31` #ffffff |
| Fixed-ratio colours derived from a token | 45 | (they follow the base colour) | the same "muted" text at 60 % is declared five times: `DeviceMenu.qml:33`, `DeviceRow.qml:24`, `GitHub.qml:108`, `Notifications.qml:53`, `StatPopup.qml:28`; at 50 % in `ControlCenter.qml:70`; dividers and tile fills in `ControlCenter.qml:69-74` |
| Font sizes | 51 | 19 | `Tooltip.qml:125`; `DeviceMenu.qml` 6; `DeviceRow.qml` 5; `Clock.qml` 5 (88 px at `:107`); `ControlCenter.qml` 6; `GitHub.qml` 10; `Media.qml` 5; `Notifications.qml` 7; `StatPopup.qml` 5; `Taskbar.qml:153` (9 px) |
| Font weights | 20 (`Font.DemiBold`/`Bold`) | 1 (`Pill.qml:92`) | `Tooltip.qml:126`, `DeviceMenu.qml:63,83,145`, `DeviceRow.qml:72,118`, `ControlCenter.qml` 7, `GitHub.qml:335,357`, `Media.qml:93,104,194`, `StatPopup.qml:135`, `Taskbar.qml:154` |
| Corner radii | 22 | 8 (`radius`), 5 (`popupRadius`) | `BarWindow.qml:44`; `Tooltip.qml:111` (4, ignores `popupRadius`); `DeviceMenu.qml:74,210`; `DeviceRow.qml:38,109`; `ControlCenter.qml:301,346,355,422,492,712,942`; `GitHub.qml:94,272`; `Media.qml:145,185`; `Notifications.qml:78,153`; `StatPopup.qml:106`; `Taskbar.qml:131,144` |
| Animation times | 11 | 1 (`popupFadeMs`) | `Pill.qml:64` 150; `Tooltip.qml:18` 600 delay, `:116` 100; `DeviceMenu.qml:116` 1400; `Taskbar.qml:92,104`; `TrayIconButton.qml:49`; `WallpaperGallery.qml:93`; `WorkspaceDot.qml:38,54`; `Workspaces.qml:67` |
| Spacing, margins, paddings | 88 (32 spacing, 56 margin/padding) | 4 (`padding`), 2 (`spacing`) | heaviest in `GitHub.qml`, `StatPopup.qml`, `DeviceMenu.qml`, `Taskbar.qml`, `ControlCenter.qml` |
| Fixed widths and heights | 96 | 5 (`pillHeight`) | 23 in `ControlCenter.qml`, 13 in `Media.qml`, 10 in `DeviceMenu.qml`; mostly popup layout (album art, tiles, icons) |
| Glyphs | 14 defaults | n/a | all are widget properties (`Volume.qml:19`, `Battery.qml:18-23`, ...): set per instance, not per theme |

Findings:

- **The bar honours the theme; popups mostly do not.** `Pill`, `BarLayout`, `Workspaces` and the
  popup chrome (`PopupWindow.qml:132-133`) read `Theme`. The content of popups (calendar, media,
  GitHub, notifications, control centre, device menus) hard-codes its type scale, weights, radii
  and spacing. A `fontSize: 15` in `theme.qml` changes the bar and leaves 51 popup texts as they were.
- **The owner's palette has leaked into the engine.** #949cbb, #737994, #51576d, #babbf1, #ca9ee6 and
  rgba(35,38,52) are Catppuccin Frappe colours from the owner's `theme.qml` and Python
  `styles.css`. They look right with that theme and wrong with any other, including the default.
- **Tokens that exist but are barely used:** `goodFill` (0 uses), `fontWeight` (1), `spacing` (1
  file), `padding` (3 files), `iconSize` (2 files). `Tooltip` has its own chrome (`radius: 4`,
  `border.width: 1`) instead of the popup tokens.
- **Missing tokens the code invents locally:** muted text, divider, a control radius (the value 6
  appears 12 times), a title size, a display size (88 px twice), an emphasis weight (DemiBold 19
  times), a standard animation time (150 ms 6 times).
- **Discoverability:** `examples/config/theme.qml` says "the values below are the defaults" and
  lists 20 of the 37 writable tokens; the popup, island and bar-metric tokens are missing.
  `docs/styling.md` is a three-line stub.

## 2. Embedding versus loading from disk

### 2.1 What embedding buys

| Claimed benefit | Assessment |
|---|---|
| Ahead-of-time compilation (qmlcachegen) | Real. Bindings and functions are compiled to C++ (the comment in `qml_modules.cmake` cites 550 compiled functions for `wabar.widgets`); syntax and type errors in built-ins fail the build, not the user's start. The runtime benefit is small for a bar that is idle most of the time. |
| Startup time | Not measured (this proposal builds and runs nothing). Loading 5.3k lines of QML from source means parsing and compiling them at start; the user config already pays that cost on every start because the disk cache is off. Measure it before deciding (step 1 of section 5). |
| Single-file deploy | Not a property wabar has. `dist/wabar` is a folder: Qt runtime from `windeployqt`, `WabarTrayHook*.dll`, `wabarc.exe`, `modules/`, `examples/` (`cmake/wabar_deploy.cmake`). |
| Safety | Real, and the main reason to keep the compiled copy. Built-in QML always matches the `wabar.services` C++ it calls, a user cannot corrupt it, and `--safe` and the fallback bar always work. |

### 2.2 What Qt supports (verified against the Qt 6 docs)

- **Import path order.** By default: the application binary's directory, then `qrc:/qt-project.org/imports`,
  then `qrc:/qt/qml`, then `QML_IMPORT_PATH`, then Qt's own imports. `QQmlEngine::addImportPath`
  puts the new path *first*. So a directory added with `addImportPath`, or a URI-shaped folder next
  to `wabar.exe` (`<exe dir>/wabar/widgets/qmldir`), is found before the compiled module.
- **`prefer`.** A `qmldir` line `prefer :/qt/qml/wabar/widgets/` tells the engine to load the
  module's files from that path whatever directory the `qmldir` was found in. CMake generates it so
  that AOT-compiled files are used. A disk override therefore needs a `qmldir` *without* that line
  (or with `prefer` pointing at the disk folder). The C++ half of a module (`plugin`, `classname`
  lines) must stay, so that a disk `qmldir` for `wabar` still registers `Config`, `Bar` and `Theme`.
- **Loss of AOT on disk.** Files loaded from disk run in the interpreter or JIT; the compiled C++
  for those files is not used. The runtime disk cache (`.qmlc` under `QStandardPaths::CacheLocation`
  / `qmlcache`, or `QML_DISK_CACHE_PATH`) only stores byte code and is off in wabar
  (`QML_DISABLE_DISK_CACHE=1`). Qt's build system cannot produce `.qmlc` files ahead of time.
- **Hot reload.** `clearComponentCache()` drops only unreferenced components; objects made from old
  components keep their old types. The host already tears every bar down before clearing, so
  built-ins loaded from disk would reload through the same path. Two exceptions: `BarHost` keeps
  `windowComponent_` for the life of the host and loads it by a fixed `qrc:` URL
  (`bar_host.cpp:22,54`), so `BarWindow.qml` never reloads; and QML singletons survive
  `clearComponentCache()` (only `clearSingletons()` drops them, and it would also drop the C++
  service singletons and their threads). Keep `Theme` a C++ singleton for that reason.
- **Directory import versus module import (claim in `docs/architecture.md:146`).** Qt documents:
  "Directory Imports rank below any module imports in precedence. If the same name is defined in a
  module and in a directory that are both imported into the same namespace, only the module's type
  is made available." Import order does not matter. So a `widgets/Clock.qml` next to
  `import wabar.widgets` is silently ignored, and the architecture document's "relies on import
  order" should say "does not work". It works when the built-ins are imported qualified
  (`import wabar.widgets as W`) or when the copy lives in a module of its own on the import path.
- **Shadowing never reaches inside a module.** Even a working shadow only changes the names in
  `bar.qml`. `Volume.qml` resolves `Pill` from its own module, so a user `Pill` cannot restyle the
  16 built-ins that use it. Only replacing the module itself (O1) or a theme hook does that.
- **Precedent for a style as files.** Qt Quick Controls styles are QML folders on the import path
  (`-style MyStyle`, `QQuickStyle::setStyle`), with a fallback style imported by the style's
  `qmldir` (`import QtQuick.Controls.Basic auto`), and every control has replaceable `background`
  and `contentItem` items. wabar's built-ins are not Controls, but the two ideas carry over: a style
  can be a folder of files, and a part can be a replaceable component.

## 3. Design options

### O1. Ship the built-in QML as files and prefer them over the compiled copy

`deploy` writes `dist/wabar/modules/wabar/{,widgets/,services/}` with `qmldir` files that lack
`prefer`. `wabar.exe` adds the folder with `addImportPath` when it exists, carries a build id in
each `qmldir` and ignores a folder whose id differs from its own (version skew with
`wabar.services`). `--modules-dir <dir>` points at another folder (the owner's working copy).
`--safe` and the fallback bar never add it. The host watches the folder and recreates
`windowComponent_` on reload.

Caveats: AOT is lost for those files; the "fallback to the compiled copy" is only per start (the
import path is fixed when the engine first resolves a module; whether a changed path takes effect
after `clearComponentCache()` is not documented and must be tested); the dev loop needs the
flattened layout, because the sources are spread over `src/features/*/qml` and flattened only by
`QT_RESOURCE_ALIAS`, so either a sync step copies them or a generated `qmldir` points into the
source tree with relative paths (not verified that sibling types resolve then).

### O2. Richer data-driven tokens

Widen the theme so variants are data: a type scale and the missing tokens from section 1.4, paint
values (colour, colour list, gradient) for every background, and grouped per-part objects
(`Theme.island.start.fill`, `Theme.pill.radius`, `Theme.popup.titleSize`). Keep `Theme` a C++
singleton; keep the user's `ThemeSpec` object alive after the reload instead of copying and
destroying it (`bar_host.cpp:162-168`), and expose it as `Theme.spec` so a user widget can read
tokens the user invented (`property color brand: "#ff8800"` in `theme.qml`, `Theme.spec.brand` in
the widget). Built-ins read only tokens; a test enforces it.

### O3. A style layer keyed by widget class and variant

Each built-in declares a style class (`"clock"`, `"cpu"`, `"island.start"`) and sets a variant
from its state (`"warn"`, `"charging"`, `"focused"`). `theme.qml` holds a `styles` map from
`class` or `class.variant` to a property bundle. One C++ function merges, in fixed order:
part base (`"pill"`), class, class plus variant. The building blocks (`Pill`, the island, the popup
frame, the tooltip) apply the bundle; widgets add no per-property code. This is CSS classes without
selectors, cascade or a parser.

### O4. A declarative stylesheet parsed at runtime

A CSS-like file mapped to item properties. Python YASB got this almost free: Qt Widgets has a
stylesheet engine, and YASB adds 171 lines of preprocessing (`css_processor.py`: imports,
variables, `-qproperty-`). Qt Quick has no stylesheet engine. The earlier C++ port of YASB had to
rebuild the missing parts for Widgets and still needed `core/css` (preprocessor 262 lines, rules
1,310, transition engine 838) plus a 14k-line vector file; all of it was deleted in `03b3ccd`. For
Qt Quick it would also need selector matching on the item tree, specificity, pseudo-states and a
property mapping per item type, while `theme.qml` already hot reloads, type-checks and lints.

### O5. Override a built-in by shadowing its type

What exists today, and it does not work as documented (section 2.2): a same-named file in
`widgets/` loses to the module. It can be made to work with a qualified import of `wabar.widgets`
or a user overlay module, but it forks a whole widget (upgrades do not reach the copy) and cannot
restyle the building blocks other built-ins use.

### Evaluation

| Criterion | O1 files on disk | O2 rich tokens | O3 style layer | O4 stylesheet | O5 shadowing |
|---|---|---|---|---|---|
| No rebuild for look changes | yes, but by editing engine files | yes, within the token set | yes, within the declared classes and parts | yes, within the mapped properties | yes, by forking a widget |
| Hot reload | yes, after the host changes in O1 | yes, today's path | yes, today's path | needs its own watcher and re-apply | yes, today's path |
| Startup cost | AOT lost for built-ins; measure | none | one merge per widget, small | parse plus matching on every reload | none |
| Testability | needs a precedence test and a skew test | good: a theme sweep test | good: pure merge function plus the sweep test | hard: a CSS engine to test | poor |
| Complexity | medium (import path, `qmldir` rewrite, watcher) | low to medium | medium | high | none, but a doc fix |
| Upgrade safety | poor if users edit `dist`: overwritten or forked | good: tokens are an API with defaults | good, if class and part names are kept stable | fair | poor: copies rot |
| Discoverability | good for developers (read the source) | good with qmlls completion on typed groups | fair: string keys need a generated table | good for CSS users | poor |
| Intent over framework | neutral | good: tokens name intent (muted, divider) | good: classes are the feature names | poor: selectors name item structure | poor |

## 4. Recommendation

Adopt O2 and O3 together as the user-facing contract, add replaceable backgrounds for the four
building blocks, and use O1 only as the owner's development loop behind `--modules-dir`. Reject O4.
Fix the O5 documentation and keep it as the escape hatch for real forks, renamed copies only.

The rule that makes it hold: **a built-in widget reads no literal colour, size, font, radius,
spacing or duration; it reads a token or a style bundle.** Layout geometry intrinsic to a widget
(the calendar's 7 columns, album art aspect) stays code.

### 4.1 Target design

Config home, unchanged layout:

```
bar.qml          bars and layout
theme.qml        ThemeSpec: palette and metric tokens, `styles`, background components
widgets/         user widgets
```

Engine pieces (names are proposals):

| Piece | Where | What |
|---|---|---|
| `ThemeValues` | `src/host/qml/bar_theme.h` | the 37 tokens, plus the missing ones: `textMuted`, `divider`, `controlRadius`, `titleFontSize`, `displayFontSize`, `emphasisWeight`, `animationMs`, `tooltipRadius`. Derived tokens stay read-only ratios of the palette. |
| `ThemeSpec.styles` | same | `QVariantMap`: key `class` or `class.variant`, value a bundle (`fill`, `hoverFill`, `textColor`, `iconColor`, `radius`, `padding`, `fontSize`, `fontWeight`, `background`). |
| `ThemeSpec` default property | same | child objects, so a `Component { id: ... }` can be declared in `theme.qml` and used in a bundle. |
| `Theme.spec` | same, kept alive by `BarHost::Loaded` | the user's `ThemeSpec` object, for tokens the user invents. |
| `Theme.style(keys)` | `bar_theme.cpp` | merges bundles in order: `[part, class, class.variant]`; later wins; unknown keys are ignored and logged once per reload. |
| paint value | `src/ui/PaintedRect.qml` (new) | draws a paint: a colour, a list of colours (horizontal, evenly spaced, as `startFill` today) or `{ stops, angle, space }` where `space` is `"self"` or `"bar"` (one gradient across all islands, Python's effect). A plain colour uses a `Rectangle`; only a gradient uses the canvas. |
| parts | `Pill`, the island in `BarLayout`, `PopupWindow`, `Tooltip` | each reads `Theme.style([...])` and, when the bundle has a `background` component, loads it in place of its own body with the geometry as required properties (`width`, `height`, `radius`, `hovered`, `flipped`). |

How a built-in declares its hooks: it names a class and a variant, nothing else.

```qml
// Cpu.qml (target; the rest of the file is unchanged)
Pill {
    id: root
    styleClass: "cpu"
    variant: source.ready && source.percent >= root.warnAbove ? "warn" : ""
    // no `warnFill` property and no `fill:` binding: the "cpu.warn" bundle carries the colour
}
```

`Pill` resolves `Theme.style(["pill", styleClass, styleClass + "." + variant])` and applies it.
Precedence: a property set on the instance in `bar.qml`, then the bundle, then the flat tokens,
then the C++ defaults. Each built-in documents its class and variants in its header comment, and a
generated table in `docs/styling.md` lists them.

### 4.2 The gradient islands in the target design

A user-side `theme.qml` change, with no engine change:

```qml
import wabar

ThemeSpec {
    islands: true
    styles: ({
        "island.start":  { fill: ["#ff6a00", "#e8338a", "#8a3ffc"] },
        "island.center": { fill: ["#8a3ffc", "#2f6bff"] },
        "island.end":    { fill: ["#2f6bff", "#1fb5c9"] }
    })
}
```

One gradient running across the whole bar is `"island": { fill: { stops: [...], space: "bar" } }`.
Something the paint value cannot express (a glow, an image, a border on one island) is a
background component, still in `theme.qml`:

```qml
ThemeSpec {
    Component { id: glass; Rectangle { color: "#3328c4f5"; border.color: "#8828c4f5" } }
    styles: ({ "island.end": { background: glass } })
}
```

The automatic centre bridge of today's `centerStops` becomes an explicit `island.center` entry;
`BarLayout.startFill`, `centerFill` and `endFill` stay for one release as aliases that write the
same bundles.

### 4.3 What remains class C

New widgets and services; new data (a new field from a service); new interaction (a new popup, a
new gesture); new layout models (a vertical bar, a fourth group, an island per widget instead of
per group); new part types (if a future part is neither a pill, an island, a popup nor a tooltip,
it gets a class and a background hook when it is added); anything Win32 (backdrops, app bar). The
shape of an island (rounded body, concave fillets) also stays code, as it does in Python.

## 5. Migration plan

Every step ends with `tools\check.ps1` green and is one or a few commits.

| # | Step | Size | Risk | Test |
|---|---|---|---|---|
| 1 | Fix `docs/architecture.md` on shadowing; write `docs/styling.md` from section 1.4; list all 37 tokens in `examples/config/theme.qml` | S | none | a test that `examples/config/theme.qml` names every writable `ThemeValues` property |
| 2 | Prove the import rules: a same-named `widgets/Clock.qml` loses to `wabar.widgets`; a qualified import lets it win | S | none | `ThemeHost`-style harness test, documents Qt's behaviour |
| 3 | Measure: time from `main` to first frame, compiled built-ins against built-ins loaded from a disk copy without `prefer` | S | none | numbers recorded in this document |
| 4 | Remove the palette leak: the 13 colours of section 1.4 become tokens or ratios of tokens; add `textMuted` and `divider` and replace the 45 ad hoc ratios | S | low: visible change only under the default theme | theme sweep test (below) for colours |
| 5 | Add the type scale, weight, radius and animation tokens; replace the 51 font sizes, 20 weights, 22 radii and 11 durations | M | low to medium: popups may shift by a pixel | theme sweep test extended to fonts, radii |
| 6 | The theme sweep test: instantiate every built-in widget and open its popup offscreen with a theme of unusual values (`fontSize: 17`, `text: "#123456"`), walk the item tree, fail on any `Text` or `Rectangle` whose colour or size is not derived from the theme. An allowlist holds intrinsic geometry, with a reason per entry. | M | low | is the test |
| 7 | Keep the `ThemeSpec` alive, add `Theme.spec`, the default property and `Theme.style(keys)` | S | low | unit tests of the merge order, unknown keys and reload reset |
| 8 | `styleClass` and `variant` on `Pill`; every built-in sets them; per-widget colour properties (`warnFill`, `lowFill`, `chargingFill`) become default bundles | M | medium: touches every widget | sweep test plus one test per variant (a `cpu.warn` bundle recolours `Cpu` at high load) |
| 9 | Paint values and `PaintedRect`; the island reads `island.*` bundles; `startFill` and friends become aliases | M | medium: canvas cost if every pill used it, hence the plain-colour path | the existing gradient tests rewritten against bundles; a test that a `theme.qml` change alone turns the islands into gradients |
| 10 | Background components for pill, island, popup and tooltip | M | medium | a test that a `background` component replaces the body and receives the geometry |
| 11 | O1 for the owner: `--modules-dir`, `qmldir` without `prefer`, build id check, `BarWindow` reloadable, the folder watched | M | medium: import path and skew | a disk `Clock.qml` overrides the compiled one; `--safe` ignores it; a stale build id is ignored with a log line |

Steps 1 to 6 need no new mechanism and turn most class A failures into real class A. Steps 7 to 10
make class B user-side. Step 11 is independent and can go first if the owner's edit loop matters
more than the user contract; it does not remove the cause.

## 6. Open questions for the owner

1. Is O1 for the owner only (`--modules-dir`, never in `dist`), or should users be able to edit
   built-in QML in their install?
2. String keys in a `styles` map (flexible, no completion) or typed grouped objects
   (`Theme.island.start.fill`, completion in qmlls, but every new group is engine code)? This
   proposal uses typed tokens for the palette and metrics and the map for classes and variants.
3. Should built-ins default to the neutral default theme, or keep the Catppuccin Frappe colours of
   the Python setup as defaults (and move them into the default token values)?
4. Keep `BarLayout.startFill`, `centerFill`, `endFill` as aliases for one release, or drop them now
   (one user, pre-1.0)?
5. Are light and dark variants or named theme files in scope for this work, or later?
6. If step 3 shows a measurable startup cost for disk-loaded built-ins, is that acceptable in
   release builds, or must release always run the compiled copy?

## 7. What I could not determine

- Startup and memory cost of loading the built-ins from disk: nothing was built or run.
- Whether a `qmldir` whose entries point into `src/features/*/qml` with relative paths lets the
  files resolve their sibling types, which decides whether O1 needs a copy step for development.
- Whether changing the engine's import path at run time takes effect after
  `clearComponentCache()`; the Qt docs do not say, so O1 assumes the path is fixed per start.
- That a module's own types beat the types it re-exports through a `qmldir` `import` line; Qt Quick
  Controls styles depend on it, but the docs text I found does not state it.
- Whether Python YASB's island frame would show a `qlineargradient` background; it follows from
  `bar_style.py`, but I did not run it.
