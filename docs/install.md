# Install waku

This guide installs waku from the MSIX package on a GitHub release. It covers the first run,
customising the bar, updates and removal.

## Requirements

- Windows 11. The package declares Windows 10 build 19041 as its minimum.
- An x64 or an ARM64 PC. Pick the package that matches the processor:
  - Intel or AMD: `waku-x64.msix`.
  - Snapdragon and other ARM PCs: `waku-arm64.msix`.
  - To check, run `$env:PROCESSOR_ARCHITECTURE` in PowerShell. `AMD64` means x64, `ARM64` means ARM64.

## Download

Open the [latest release](https://github.com/JanDeDobbeleer/waku/releases/latest) and download
the files for your processor:

| File | Use |
| --- | --- |
| `waku-<arch>.msix` | The package. |
| `waku-<arch>.appinstaller` | The App Installer file. It installs the package and keeps it updated. |
| `checksums.txt` | SHA-256 sums of the release files. |

`<arch>` is `x64` or `arm64`. You can check a download against `checksums.txt`:

```powershell
Get-FileHash .\waku-x64.msix -Algorithm SHA256
```

## Install

Use one of these methods.

- Double-click `waku-<arch>.msix` and choose Install.
- Double-click `waku-<arch>.appinstaller` and choose Install. Windows then also checks the release
  for new versions (see [Updates](#updates)).
- From PowerShell:

  ```powershell
  Add-AppxPackage -Path .\waku-x64.msix
  ```

  Or install straight from the release:

  ```powershell
  Add-AppxPackage -AppInstallerFile https://github.com/JanDeDobbeleer/waku/releases/latest/download/waku-x64.appinstaller
  ```

Replace `x64` with `arm64` on an ARM64 PC.

## First run

Start waku from the Start menu (the entry is named `waku`), or run `waku` in a terminal. The
package puts `waku.exe` and `wakuc.exe` on your `PATH`.

With no config, waku shows a built-in bar on the top edge of the primary screen. It has a clock,
workspaces, volume, battery, CPU and memory, on a blurred background with the default theme.

Check that the bar runs:

```powershell
wakuc status
```

`wakuc` talks to the running bar. `wakuc quit` stops it.

## Customise

waku finds its config home in this order:

1. `--config-home <dir>` on the command line.
2. The `WAKU_CONFIG_HOME` environment variable.
3. `%USERPROFILE%\.config\waku`.

`wakuc` resolves the folder the same way.

If the folder has no `shell.qml` (or does not exist) and you did not set the home yourself, waku
shows the built-in bar. When you create `%USERPROFILE%\.config\waku\shell.qml`, the running bar
switches to it on save.

Write a starter `shell.qml` into the config home:

```powershell
wakuc config init
```

This creates the folder if it is missing and writes the built-in bar as `shell.qml`, so you can
edit it. It does not need a running bar. If a `shell.qml` exists, the command stops with exit code 1
and changes nothing; add `--force` to replace it. The package also ships a fuller sample in
`examples\config` under the package folder (or see [examples/config](../examples/config) in the
repository).

The config home holds:

- `shell.qml`: which bars to show and which widgets go where.
- `theme.qml`: colours and sizes (optional).
- `widgets/`: your own widgets, one component per file (optional).

Edit a file and save. The bar reloads on its own. A mistake keeps the previous shell running and
shows an error pill on the bar. To reload by hand:

```powershell
wakuc reload
```

Commands and flags are in [src/app/README.md](../src/app/README.md).

## Optional companions

waku runs without them. Install the ones you need:

- [komorebi](https://github.com/LGUG2234/komorebi) or [GlazeWM](https://github.com/glzr-io/glazewm):
  a tiling window manager. The Workspaces widget shows its workspaces. waku has no native Windows
  virtual desktop support yet, so the widget needs one of them.
- [whkd](https://github.com/LGUG2234/whkd) or [AutoHotkey](https://www.autohotkey.com/): a hotkey
  tool. Bind a key to `wakuc shortcut <name>` to fire a `GlobalShortcut` of your shell. The example
  is in [docs/shells.md](shells.md).

## Run at sign-in

The package does not start waku by itself. One command registers a per-user Scheduled Task that
starts waku when you sign in. It runs faster than a Startup folder shortcut, which Explorer delays
by up to a minute:

```powershell
wakuc startup enable
```

The task runs the bare `waku.exe`, so `waku.exe` must be on your user PATH. The package puts an
execution alias there. If it is missing, `enable` registers nothing and tells you which folder to
add. A change to the PATH reaches new sessions only: sign out and in again, then run the command
again.

The task starts waku with `--register-app-bar --hide-taskbar`. Change that with:

- `--config-home <dir>`: start waku with that config home. Without it, waku uses its own default
  (`WAKU_CONFIG_HOME`, else `~/.config/waku`).
- `--no-app-bar`: leave out `--register-app-bar`.
- `--keep-taskbar`: leave out `--hide-taskbar`.
- Other `waku` flags after `--`, for example `wakuc startup enable -- --no-tray` or
  `wakuc startup enable -- --manage-komorebi`.

If `keys.conf` exists in your config home (`--config-home`, else `%USERPROFILE%\.config\waku`),
`enable` registers a second task that starts the keys daemon `wakukeys.exe` (it reads that file, and
talks to the bar of your `--config-home`). Without the file
it says so and registers nothing for the keys. `wakukeys.exe` must be on your PATH too (the package
has an alias). The daemon puts its own folder first in the PATH of the commands it runs, so a
binding can run `wakuc` without a PATH entry. To make `wakukeys` talk to a bar started with
`--config-home`, set the user environment variable `WAKU_CONFIG_HOME` to that folder.

`wakuc startup enable` again replaces the tasks. `wakuc startup status` shows whether each is
registered, its command and which `waku.exe` and `wakukeys.exe` the PATH finds. `wakuc startup
disable` removes both tasks. An old `waku.lnk` in the Startup folder is deleted on `enable`; `disable` does not bring it
back.

## Updates

The package contains an App Installer file that points at
`https://github.com/JanDeDobbeleer/waku/releases/latest/download/waku-<arch>.appinstaller`.
Windows polls that address in the background and installs a newer release. To update by hand,
install the newer `waku-<arch>.msix` over the old one.

## Uninstall

```powershell
Get-AppxPackage JanDeDobbeleer.Waku | Remove-AppxPackage
```

Your config in `%USERPROFILE%\.config\waku` and the data in `%LOCALAPPDATA%\waku` stay. The package
does not redirect writes into its own folder, so these locations are shared with unpackaged tools.
Delete them yourself for a clean slate. Run `wakuc startup disable` too if you enabled it.

## Troubleshooting

- **Bar not visible.** Run `wakuc status`. If it exits with 2, no bar is running: start `waku`.
  If a bar runs, check which screen it targets: the sample `shell.qml` uses `screens: ["primary"]`.
- **Config error pill.** Your last save has a mistake. The previous shell stays on screen. Read the
  message on the pill, fix the file and save again.
- **Start with the built-in bar.** `waku --safe` ignores your config. Use it to rule out your
  shell as the cause.
- **Tray and Explorer.** The Tray widget injects a small DLL into Explorer to see tray icons.
  `waku --no-tray` never hooks Explorer. If tray icons are missing or Explorer misbehaves, run
  with `--no-tray` to rule the hook in or out.
- **Stop the bar.** `wakuc quit`.
- **`wakuc` exit codes.** 0 ok, 1 the bar answered an error, 2 no bar is running, 3 usage error.
- **Try a config safely.** Pass a scratch folder, for example
  `waku --config-home <scratch>\waku`. Your real config stays untouched.

All `waku` flags are in [docs/building.md](building.md). Security notes are in
[SECURITY.md](../SECURITY.md).

## Use a Quickshell shell or let an agent build one

waku is Quickshell-compatible: it offers the `Quickshell` modules, so shells written for
Quickshell can run with few changes. Shells are not bundled. A shell lives in your config home
(`%USERPROFILE%\.config\waku`) and carries its own layout and styling. The differences from
Quickshell (input masks, shortcuts without keys) are in [docs/shells.md](shells.md).

An agent can write a shell for you. Give it a prompt like this and fill in the brackets:

```text
Build a waku shell for me.

- waku is a Windows top bar configured in QML, and it is Quickshell-compatible. Read
  docs/shells.md in https://github.com/JanDeDobbeleer/waku first.
- Write the shell into a scratch folder, [<scratch>\my-shell], with shell.qml (and theme.qml if
  needed). Run it with: waku --config-home [<scratch>\my-shell]
- The bar hot reloads when a file is saved. Edit, save, look at the result, repeat. A mistake shows
  an error pill and keeps the previous shell.
- Use wakuc --config-home [<scratch>\my-shell] status|reload|call to talk to that bar.
- The target look is in the attached screenshots: [describe or attach them].
- Do not touch %USERPROFILE%\.config\waku until I say the shell is done. Then copy the folder
  there.
```

## See also

- [README.md](../README.md): what waku is.
- [docs/building.md](building.md): build from source and every flag.
- [src/app/README.md](../src/app/README.md): `waku` and `wakuc`.
