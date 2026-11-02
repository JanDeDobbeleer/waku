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

The package ships a sample config in `examples\config` under the package folder. Copy it as a
starting point:

```powershell
$pkg = (Get-AppxPackage JanDeDobbeleer.Waku).InstallLocation
$cfg = Join-Path $env:USERPROFILE '.config\waku'
New-Item -ItemType Directory -Force -Path $cfg | Out-Null
Copy-Item -Path (Join-Path $pkg 'examples\config\*') -Destination $cfg -Recurse
```

If Windows denies access to the package folder, copy
[examples/config](../examples/config) from the repository instead.

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
  a tiling window manager. The Workspaces widget shows its workspaces.
- [whkd](https://github.com/LGUG2234/whkd) or [AutoHotkey](https://www.autohotkey.com/): a hotkey
  tool. Bind a key to `wakuc shortcut <name>` to fire a `GlobalShortcut` of your shell. The example
  is in [docs/shells.md](shells.md).

## Run at sign-in

This is a manual step. The package does not register a startup task. Put a shortcut to the
packaged app in your Startup folder:

```powershell
$family = (Get-AppxPackage JanDeDobbeleer.Waku).PackageFamilyName
$link = Join-Path ([Environment]::GetFolderPath('Startup')) 'waku.lnk'
$shell = New-Object -ComObject WScript.Shell
$s = $shell.CreateShortcut($link)
$s.TargetPath = 'explorer.exe'
$s.Arguments = "shell:AppsFolder\$family!Waku"
$s.Save()
```

Delete `waku.lnk` from the Startup folder (`shell:startup`) to undo it.

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
Delete them yourself for a clean slate. Remove the Startup shortcut too if you added one.

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
  `waku --config-home D:\scratch\waku`. Your real config stays untouched.

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
- Write the shell into a scratch folder, [D:\scratch\my-shell], with shell.qml (and theme.qml if
  needed). Run it with: waku --config-home [D:\scratch\my-shell]
- The bar hot reloads when a file is saved. Edit, save, look at the result, repeat. A mistake shows
  an error pill and keeps the previous shell.
- Use wakuc --config-home [D:\scratch\my-shell] status|reload|call to talk to that bar.
- The target look is in the attached screenshots: [describe or attach them].
- Do not touch %USERPROFILE%\.config\waku until I say the shell is done. Then copy the folder
  there.
```

## See also

- [README.md](../README.md): what waku is.
- [docs/building.md](building.md): build from source and every flag.
- [src/app/README.md](../src/app/README.md): `waku` and `wakuc`.
