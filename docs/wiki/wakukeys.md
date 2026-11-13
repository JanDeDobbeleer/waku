# wakukeys: global hotkeys

`wakukeys` reads a `keys.conf` file, registers global hotkeys and runs their actions. It uses
whkd-style bindings and does not require whkd or AutoHotkey.

This guide describes the current development version. Your installed release may differ.

## Get started

Create `keys.conf` in your config home. With the default config home, the path is
`%USERPROFILE%\.config\waku\keys.conf`. For example:

```text
.shell cmd

alt + shift + o : waku:reload
alt + left : wakuc tile focus left
alt + t : start "" wt
```

When waku starts on a live desktop and finds `keys.conf`, it starts `wakukeys` unless a daemon is
already running or waku was started with `--no-keys`. You can also start the daemon yourself:

```powershell
wakukeys
wakukeys status | Out-String
```

Only one daemon runs per user. The example's tile command needs a running bar with tiling enabled.
See the [sample keys.conf](https://github.com/JanDeDobbeleer/waku/blob/main/examples/config/keys.conf)
for more bindings.

## Start options

```text
wakukeys [--config <file>] [--config-home <dir>] [--log-file <file>]
```

- `--config <file>`: use this binding file instead of `keys.conf` in the config home.
- `--config-home <dir>`: select the config home for the default binding file and the bar that
  actions control. Without it, `WAKU_CONFIG_HOME` is used if set, otherwise
  `%USERPROFILE%\.config\waku`.
- `--log-file <file>`: write daemon diagnostics to a file. Logging to a file is off by default.

Each option also accepts `--option=value`. For example:

```powershell
wakukeys --config-home 'C:\Users\Example\my shell' --log-file 'C:\Temp\wakukeys.log'
```

The daemon has no console window and continues after the terminal closes. A second start fails
while the daemon is running. To register it at sign-in, see
[wakuc startup](wakuc#startup).

## Control commands

```powershell
wakukeys status | Out-String
wakukeys reload | Out-String
wakukeys stop | Out-String
```

- `status` reports the process ID, active bindings, conflicts, issues, pause state and config file.
- `reload` reads the binding file again. The daemon also watches the file for changes.
- `stop` ends the daemon.

These commands take no options and control the user's one daemon, regardless of its config home.
Because `wakukeys` is a Windows GUI program, PowerShell may return to the prompt before a bare
command prints its answer. Piping to `Out-String` waits for it.

On reload, an unreadable file or a file with errors and no valid bindings leaves the old bindings
in place. If some bindings are valid, those load and invalid lines are reported. A failed initial
load stops the daemon.

## Binding syntax

Each binding is one line: modifiers and one key, then a colon and an action. Modifiers are `alt`,
`ctrl` (or `control`), `shift` and `win`. They are case-insensitive; left and right variants count
as the same modifier. Key names include letters, digits, `f1` through `f24`, `left`, `right`,
`up`, `down`, `space`, `return`, `escape`, `tab`, `oem_1` through `oem_8` and media keys.

```text
# Comments start with #
alt + shift + left : wakuc tile move left
win + space : waku:shortcut launcher
```

A comment can also follow a command when `#` follows a space and is outside quotes. The first
binding wins when a key is duplicated. A registered hotkey is consumed even when a per-app rule
or blacklist makes it do nothing.

### Directives and app-specific bindings

```text
.shell cmd
.pause alt + shift + p
.pause_hook echo pause toggled
.blacklist [ "game.exe", "vlc.exe" ]

alt + n [
    notepad.exe : echo Notepad
    Default : echo Other application
]
```

- `.shell` chooses `cmd`, `powershell` or `pwsh` for shell commands. The default is
  `powershell`; `pwsh` must be on PATH. PowerShell commands run without a profile.
- `.pause` assigns a key that toggles the other bindings. `.pause_hook` runs a command whenever
  pause changes.
- `.blacklist` skips actions while a named executable is in the foreground.
- A per-app block selects a command by foreground process name. `Default` handles other apps;
  `Ignore` skips the action. Matching ignores case and the `.exe` suffix.

See the [keymap syntax and full key names](https://github.com/JanDeDobbeleer/waku/blob/main/src/features/keymap/design.md#syntax)
for details.

## Actions

A command not starting with `waku:` runs in the selected shell. The daemon starts commands
without a console window, discards their output and does not wait for them. It puts its own folder
first on the command's PATH and passes its selected config home through `WAKU_CONFIG_HOME`, so
`wakuc` commands reach the same bar.

The built-in actions avoid starting a shell for bar and daemon commands:

```text
waku:shortcut <name>
waku:surface show|hide|toggle <name>
waku:call <target> <function> [arguments...]
waku:reload
waku:keys-reload
waku:keys-stop
waku:activate-or-launch <exe-or-title> <command>
```

`activate-or-launch` focuses a matching visible window, restoring it if minimized; if none matches,
it runs the command in the selected shell. It matches the executable first, then a title
substring. Use double quotes around a target with spaces:

```text
win + t : waku:activate-or-launch "Windows Terminal" start "" wt
```

`waku:` action names are case-insensitive. Use double quotes to group arguments and double an
embedded quote. Unknown or malformed actions are reported as binding errors.

## Exit codes and troubleshooting

- `0`: success.
- `1`: daemon error, an explicitly specified file is missing, a failed initial load, or a daemon
  is already running.
- `2`: no daemon is running for a control command.
- `3`: invalid usage, a missing default key file, or a control communication failure.

If a binding does not fire, check `status` for conflicts and issues and use `--log-file` when
starting the daemon. Another application or Windows may already own the hotkey. Check the
selected config home if a `waku:` action or `wakuc` command reaches the wrong bar.

## Related documentation

- [wakuc command reference](wakuc)
- [waku startup options](waku)
- [Keymap design and limitations](https://github.com/JanDeDobbeleer/waku/blob/main/src/features/keymap/design.md)
