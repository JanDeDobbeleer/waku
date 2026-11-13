# Getting started

## Install and start waku

Follow the [installation guide](Installation), then start waku from the Start menu or run `waku`.
With no config, it shows the built-in bar. Check it with:

```powershell
wakuc status
```

By default, waku hides the Windows taskbar, reserves space for panels that request it and tiles
windows. `wakuc quit` stops the instance and restores waku's taskbar changes.

## Try a ready-made shell

1. Download [waku-ports](https://github.com/JanDeDobbeleer/waku-ports) and extract it to a folder.
2. Read the chosen port's README. Ports can need their own environment variables or setup steps.
3. From the extracted folder, use the port launcher's `-DryRun` to inspect its command:

   ```powershell
   .\start.ps1 -Port nomarchy -DryRun
   ```

4. When ready, follow the port's launch instructions. The normal launcher stops running waku
   instances, then starts the port with waku's desktop defaults. The ports README explains `-Dev`,
   its bundled-engine requirement, and how to stop a port.

Do not copy a port over an existing config without a backup. A port's folder is its config home;
its `shell.qml` and supporting files need to stay together.

## Try your own configuration

This example creates a separate starter config. Choose a new folder; `config init` refuses to
overwrite an existing `shell.qml`.

```powershell
wakuc --config-home 'C:\Temp\waku-test' config init
waku --config-home 'C:\Temp\waku-test' --no-app-bar --keep-taskbar --no-tray --no-keys --no-tiling
wakuc --config-home 'C:\Temp\waku-test' status
```

Use the same config home with every `wakuc` command for this instance. To reload after an edit or
stop it:

```powershell
wakuc --config-home 'C:\Temp\waku-test' reload
wakuc --config-home 'C:\Temp\waku-test' quit
```

Continue with [customization](Customization), [hotkeys](wakukeys) or
[troubleshooting](Troubleshooting).
