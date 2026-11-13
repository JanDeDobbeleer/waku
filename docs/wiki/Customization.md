# Customize a shell

A config home contains `shell.qml` and can also contain `theme.qml`, `widgets\` and `keys.conf`.
Start with a separate config as shown in [Getting started](Getting-started). Ported shells carry
their own styling and settings; consult the port's README before changing them.

## Arrange built-in widgets

For a starter shell using `waku.widgets`, edit `shell.qml`. This panel puts a clock at the start
and volume and battery at the end:

```qml
import waku
import waku.widgets

ShellRoot {
    Panel {
        edge: Panel.Top
        thickness: 40
        content: PanelLayout {
            start: [ Clock { format: "HH:mm" } ]
            end: [ Volume {}, Battery {} ]
        }
    }
}
```

`Panel` supports top, bottom, left and right edges. `PanelLayout` has `start`, `center` and `end`
groups. The [sample shell](https://github.com/JanDeDobbeleer/waku/blob/main/examples/config/shell.qml)
shows more widgets.

## Set a theme

Create `theme.qml` beside `shell.qml`. `ThemeSpec` properties not set here keep their defaults:

```qml
import waku

ThemeSpec {
    accent: "#5b8def"
    surface: "#66101018"
    fontSize: 13
    spacing: 12
}
```

Built-in widgets use `Theme`; a ported shell may use its own theme instead. See
[the sample theme](https://github.com/JanDeDobbeleer/waku/blob/main/examples/config/theme.qml)
and [QML reference](QML-reference).

## Add a key binding

Put a `keys.conf` beside `shell.qml`:

```text
.shell cmd
alt + shift + o : waku:reload
alt + left : wakuc tile focus left
```

waku starts `wakukeys` when it starts with a `keys.conf`, unless keys are disabled or a daemon is
already running. Changes to the file reload automatically. See the [hotkey guide](wakukeys) for
syntax, conflicts and per-app bindings.

## Apply changes

If waku was started without `--watch`, reload with `wakuc --config-home <dir> reload`. Otherwise,
changes to QML files and `qmldir` are watched. If the shell reports an error, inspect the error
message and undo or fix the edit. Keep a backup before replacing your normal configuration.
