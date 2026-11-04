# keymap

`wakuc keys` is a hotkey daemon: it loads a whkd binding file, registers the keys and runs the
commands. A user needs no whkd or AutoHotkey. The daemon is part of `wakuc.exe`. `waku.exe` does
not link this feature and does not change.

Phases 1 and 2 only. Not built: a low-level keyboard hook, chords, hotstrings, scripting.

## Commands

| Command | Effect |
|---|---|
| `wakuc keys [--config <file>] [--foreground] [--log-file <file>]` | Start the daemon. |
| `wakuc keys reload` | Read the file now. Prints `ok: N bindings active; ...` or `error: ...`. |
| `wakuc keys stop` | End the daemon. |
| `wakuc keys status` | `ok: running; pid=P; bindings=N; conflicts=C; issues=I; paused=no; config=F`. |

`--config` defaults to `%USERPROFILE%\.config\waku\keys.conf` when that file exists. No other
location is assumed (the whkd location is not read). `--config-home <dir>` selects the bar that the
`waku:` actions talk to, as for every `wakuc` command. Exit codes: 0 ok, 1 error (a bad first file,
already running, did not start), 2 daemon not running, 3 usage.

## Decisions

- **Reuse of the kernel hotkey table.** The daemon uses `kernel::Hotkeys` (add, remove),
  `HotkeyCombo` and the `RegisterHotKey` backend unchanged. `RegisterHotKey` with a null window
  posts `WM_HOTKEY` to the thread queue; the backend reads it in a native event filter, which
  `QCoreApplication`'s Windows dispatcher also calls. So it works in a console program without a
  `QGuiApplication`. `liveDesktop()` is true there (it is false only on the offscreen platform).
  No kernel change. No new singleton: the daemon uses the existing `Hotkeys` one.
- **Detaching: re-launch, not `FreeConsole`.** `wakuc keys` starts `wakuc keys --foreground ...`
  with `CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP` and waits up to 6 s for it to answer `status`.
  No console window flashes, closing the terminal does not end the daemon, and Ctrl+C in the
  terminal does not reach it. Commands it starts inherit its hidden console, so none shows a
  window. `FreeConsole` in the same process would flash a window and leave the shell blocked until
  the process had detached.
- **One daemon per user.** The daemon serves a control pipe (`control::ControlServer`), whose
  `Local\` mutex is the guard. The pipe name hashes `keys:<USERNAME>`. `reload`, `stop` and
  `status` are lines on that pipe. A second start prints the running status and exits 1.
- **A bad file never replaces a good one.** A file that cannot be read, or has no valid binding
  and at least one error, leaves the previous bindings. A file with some bad lines loads the good
  lines; each bad line is logged with its number. A first load that fails ends the start.
- **Reload on change.** `QFileSystemWatcher` watches the file and its folder (an editor that saves
  by rename drops the watch on the file). A change restarts a 400 ms timer; then the file is read
  again, and a file whose text is unchanged does nothing. `wakuc keys reload` always applies.
- **A taken key is not fatal.** If `RegisterHotKey` refuses a combination (another program owns
  it), the daemon logs it once per run, counts it in `conflicts` and binds the rest. The kernel
  table logs a generic warning for the same event.
- **Shell commands.** `cmd` runs `cmd.exe /c "<command>"`. `powershell` and `pwsh` run
  `-NoLogo -NoProfile -NonInteractive -WindowStyle Hidden -EncodedCommand <base64>`: the command
  is passed as UTF-16 base64, so quotes, `$` and `;` in the file reach the shell as written.
  `-NoProfile` keeps a launch fast; a command that needs the profile must dot-source it. The
  default shell is `powershell` (always installed); `pwsh` must be on the `PATH`. Every shell is
  started with `CREATE_NO_WINDOW`, stdout and stderr discarded, and not waited for
  (`kernel::startDetachedHidden`).
- **Nothing blocks the key handler.** Starting a shell and every control-pipe call run on the
  global `QThreadPool`. Finding and focusing a window (`activate-or-launch`) runs on the GUI thread
  inside the `WM_HOTKEY` handling, because Windows lets the process that received the hotkey set the
  foreground window; it only enumerates windows, which is fast.
- **`waku:` actions reuse the control client.** They send the same lines as `wakuc` does through
  `control::sendControlCommand`. `surface show|toggle` first calls `AllowSetForegroundWindow` for
  the bar's pid (from `status`), as `wakuc surface show` does. No process is spawned.
- **Pressed keys are consumed.** `RegisterHotKey` takes the key from the focused program. A
  binding that does nothing for the foreground app (`Ignore`, the blacklist, no rule) still
  consumes the key; the program does not see it. See the open points.

## Syntax

The file is the whkd one. Source: the whkd README and the key names of its `win-hotkeys` crate.
The parser is written from that description, not from whkd's code (whkd's licence does not allow
reuse). One line is one item.

| Item | Syntax | Notes |
|---|---|---|
| Binding | `alt + shift + oem_4 : command` | Keys are split at `+`. Modifiers: `alt`, `ctrl` (`control`), `shift`, `win`, in any order and case; `lalt`, `rctrl`, `lwin` ... count as the plain modifier because `RegisterHotKey` cannot tell the sides apart. Exactly one other key. The command is everything after the first `:`. |
| Comment | `# text` | A whole line, or after a command when `#` follows a space and is not inside `"..."` or `'...'`. |
| Shell | `.shell cmd` / `pwsh` / `powershell` | Default `powershell`. |
| Pause | `.pause alt + shift + p` | That key toggles every other binding off and on. |
| Pause hook | `.pause_hook <command>` | Runs when the pause toggles. |
| Per-app block | `alt + n [` ... `]` | Lines inside: `ProcessName : command`. Process names as `Get-Process` shows them (spaces allowed, case and `.exe` do not matter). `Default : command` applies to every app not listed. A command `Ignore` skips the key for that app. A block with no matching rule and no `Default` does nothing. |
| Blacklist | `.blacklist [ "game.exe", "vlc.exe" ]` | waku addition, not in whkd. While the foreground executable is listed, no binding runs. May span lines. |
| Line continuation | none | whkd documents none. A command is one line. |
| Duplicate keys | | The first binding wins; the later line is reported. |
| Line endings | LF or CRLF | A leading BOM is ignored. |

whkd says per-app blocks must come right below `.shell`; the parser does not require it.

Key names are the virtual-key names without `VK_`, in any case: `a`-`z`, `0`-`9`, `f1`-`f24`,
`left right up down`, `tab space return escape back`, `home end prior next insert delete`,
`numpad0`-`numpad9 multiply add subtract decimal divide separator`, `oem_1`-`oem_8`, `oem_102`,
`oem_plus oem_comma oem_minus oem_period`, `volume_up/down/mute`, `media_*`, `browser_*`,
`launch_*`, `snapshot pause capital numlock scroll apps sleep help select print execute`, and more.
Aliases: `enter`, `esc`, `backspace`, `pageup`, `pagedown`, `ins`, `del`, `printscreen`,
`capslock`. `next` is Page Down and `prior` is Page Up, as in whkd. The table is
`key_names.cpp`.

## Actions

A command that does not start with `waku:` runs in the shell. `waku:` is case-insensitive.

| Command | Does |
|---|---|
| `waku:shortcut <name>` | bar: `shortcut <name>` |
| `waku:surface show\|hide\|toggle <name>` | bar: `surface <verb> <name>` |
| `waku:call <target> <function> [args...]` | bar: `call ...`; the answer is logged |
| `waku:reload` | bar: `reload` (60 s timeout) |
| `waku:keys-reload` | read the file again (after the key handler returns) |
| `waku:keys-stop` | end the daemon |
| `waku:activate-or-launch <exe-or-title> <command>` | Focus the topmost visible top-level window whose executable is `<exe-or-title>` (case and `.exe` do not matter), else the one whose title contains it; restore it if minimized. If there is none, run `<command>` in the shell. Quote a target with spaces: `"Windows Terminal"`. |

Arguments follow the `wakuc` quoting (`control.h`): double quotes group, a doubled quote is a quote.
An unknown or malformed `waku:` command is an error of its line at load time.

## Layout

| File | Role |
|---|---|
| `keymap_parser.*`, `key_names.*`, `keymap_types.h` | Pure: text to bindings and issues. |
| `keymap_action.*` | Pure: `parseAction`, `shellInvocation`, `commandForForeground`. |
| `window_match.*` | Pure: how a name or a title matches a window. |
| `keymap_daemon.*` | The table: load, register, press, pause, run. |
| `config_watcher.*` | Debounced file watch. |
| `keymap_system.h` | Interfaces: `ProcessLauncher`, `WindowSystem`, `ControlClient`; `PipeControlClient`. |
| `win32/keymap_system_win.cpp` | The Windows `ProcessLauncher` and `WindowSystem`. |
| `testing/` | Fakes and a copy of a real `whkdrc`, for tests only. |

The daemon and its pieces are in this feature; the command line (`keys_command.*`) is in
`src/app/wakuc`. Documented Windows APIs only: `RegisterHotKey`, `EnumWindows`,
`GetWindowThreadProcessId`, `OpenProcess`, `QueryFullProcessImageNameW`, `AllowSetForegroundWindow`,
and `forceForegroundFocus` (`AttachThreadInput`, documented). No row in the undocumented API list.

## Tests

`Keymap*` (`ctest -R Keymap`). They use `ScopedFakeHotkeys`, a fake launcher, window system and
control client, and a synchronous executor. They never register a key, start a process or open the
bar's pipe. The parser test loads a copy of a real `whkdrc` (`testing/whkdrc_sample.h`). One test
watches a file in a temporary folder.

## Open points

- **Ignored keys are swallowed.** With `RegisterHotKey` a binding cannot pass the key through to the
  app it ignores. A low-level hook could; it is out of scope. Use `Default` and `Ignore` knowing
  that the key does nothing in that app.
- **Elevated foreground windows.** `OpenProcess` fails for them, so the foreground executable is
  unknown: per-app rules fall back to `Default` and the blacklist does not match.
- **Keys Windows reserves.** `Win+L`, `Ctrl+Alt+Del` and some `Win+` combinations cannot be
  registered. They count as conflicts.
- **`-NoProfile`.** PowerShell commands do not load the profile. Make it an option if a user needs
  it.
- **`wakuc startup`.** It registers one task for `waku.exe`. Starting the daemon at sign-in needs a
  second task (or an `--also` action) in `startup`. That is more than a small change, so it is not
  done; add `wakuc keys` to the Startup folder or a task by hand for now.
- **Left and right modifiers** are the same modifier.
