# Diagnostics log

`waku --log-file <path>` writes a diagnostics log. It is **off by default**: without the flag waku
writes no file. Turn it on when you want to find the cause of a strange behaviour after the fact:
a bar that came back half drawn after a dock, a reload that failed once, a crash.

```powershell
waku.exe --log-file "$env:LOCALAPPDATA\waku\waku.log"
wakuc startup enable -- --log-file "$env:LOCALAPPDATA\waku\waku.log"   # every sign-in
```

A relative path is resolved against the current folder. The folder is created. If the file cannot
be opened, waku prints one warning and runs without a log.

The file holds your config path, the command line and your screen layout. It holds no window
titles, no environment and no secrets, but read it before you attach it to a public issue.

## Format: JSON Lines

One compact JSON object per line, UTF-8. Every line is valid JSON by itself, each line is flushed
when written, and the file can be read while waku runs. Keys are in no fixed order. Read it with
anything that reads JSON, for example:

```powershell
Get-Content "$env:LOCALAPPDATA\waku\waku.log" | ForEach-Object { $_ | ConvertFrom-Json }
```

Keys on every record:

| Key | Meaning |
| --- | --- |
| `t` | Local time with the UTC offset, ISO 8601 with milliseconds. |
| `up` | Milliseconds since this run started. Use it for ordering and for durations. |
| `seq` | Counter within the run, from 1. A gap means a lost line. |
| `run` | Eight hex characters, the same on every line of one run. Several runs share a file. |
| `tid` | Thread id. |
| `k` | `log` (a log message) or `ev` (a named event). |

`log` records add `lvl` (`debug`, `info`, `warning`, `critical`, `fatal`), `msg`, and `cat` when the
message has a logging category other than the default (for example `qml`). Every `qDebug`,
`qInfo`, `qWarning`, `qCritical` and QML `console.*` call is one. Waku's own lines start with a
feature prefix such as `virtual desktops:` or `tiling:`.

`ev` records add `ev` (the event name) and the fields of that event.

## Events

| `ev` | When | Fields |
| --- | --- | --- |
| `run.start` | First record of a run. | `argv`, `configHome`, `exe`, `pid`, `version`, `qt`, `os`, `kernel`, `cpu`, `platform`, `screens` |
| `run.previous_unclean` | Right after `run.start`, when the last run in this file has a `run.start` and no `run.stop`: it crashed or was killed. | `previousRun` (its `run`) |
| `screen.changed` | A screen was added or removed, the primary screen changed, or a screen's geometry, available area, scale or DPI changed. | `why` (the Qt signal), `screen` (its name), `screens` |
| `reload.start` | A reload begins. | `cause` (`startup`, `config-files-changed`, `screens-settled`, `wakuc`, `quickshell-reload`, `api`), `hadShell`, `screenCount` |
| `reload.step` | Each step of a reload and of start-up (the `WAKU_TRACE` steps), flushed at once. After a crash the last one shows how far the reload got. | `step`, `ms` (since start) |
| `shell.reload` | The shell was reloaded, by a file change, `wakuc reload` or a screen change. | `ok`, `error` (empty when ok) |
| `run.stop` | Last record of a clean exit (`wakuc quit`, logoff). | `exit` |

A `screens` entry: `name`, `primary`, `geometry` and `available` (`x,y WxH` in device-independent
pixels), `dpr` (device pixel ratio), `logicalDpi`, `physicalDpi`, `refreshHz`. Two snapshots with a
different `dpr` or `available` show exactly what changed.

## Reading it to find a cause

1. Find the run: the last `run.start` is the current one. A `run.previous_unclean` right after it
   means the run before ended without `run.stop`; its last lines come just before that `run.start`.
2. Look at `warning`, `critical` and `fatal` records, and at every `shell.reload` with `ok:false`.
3. Line up the symptom's time (`t`, or `up`) with the nearest `screen.changed` events. A bar that
   draws at the wrong scale after a dock shows a `dpr` change in `screens` followed by a
   `shell.reload` (or the lack of one).
4. Lines from one cause are close in `up`. Read about thirty records either side of the first
   suspect before drawing a conclusion.

## Size

The file rotates to `<path>.1` when it passes 5 MiB; `<path>.1` to `<path>.3` are kept and the
oldest goes. A rotated file starts with whole lines, but not with `run.start`: the `run` key still
tells which run a line belongs to.
