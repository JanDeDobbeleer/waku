# Troubleshooting

## The bar is missing or a command cannot find it

Run `wakuc status`. Exit code 2 means no bar is running for the selected config home. Check that
waku started and pass the same `--config-home` to both programs. `WAKU_CONFIG_HOME` also selects
the instance. See [wakuc](wakuc#choose-an-instance).

## A shell or theme fails to load

Read the error shown by the bar or reported by `wakuc status`. Check `shell.qml`, imports and any
files you just edited. Start the built-in shell to isolate the configuration:

```powershell
waku --config-home 'C:\Temp\waku-recovery' --safe --no-app-bar --keep-taskbar --no-tray --no-keys --no-tiling
wakuc --config-home 'C:\Temp\waku-recovery' status
wakuc --config-home 'C:\Temp\waku-recovery' quit
```

Use an unused config home; this does not replace your normal config.

## Keys do not work

Run `wakukeys status | Out-String` and check its config path, `conflicts`, `issues` and `paused`
state. Another application or Windows may own a key. Confirm the daemon targets the same config
home as the bar and that the intended action exists. See [wakukeys](wakukeys).

## Tray icons or taskbar are missing

The waku tray needs the Windows taskbar hidden; with `--keep-taskbar`, Explorer keeps the icons.
`--no-tray` disables waku's tray host. `wakuc taskbar status` reports the taskbar state;
`wakuc taskbar show` restores what waku changed.

## Collect a log

Start a test instance with `--log-file 'C:\Temp\waku.log'`. Logs are off by default. See
[logging](https://github.com/JanDeDobbeleer/waku/blob/main/docs/logging.md) for the format and
how to read them. Review a log for private paths and other sensitive information before sharing it.
