# Security policy

## Threat model

wabar runs as the interactive user and loads that user's own QML config (`shell.qml`, `theme.qml`,
custom widgets) from `%USERPROFILE%\.config\wabar`. The config is code: it can run processes and read
files by design. Changes must neither sandbox the config nor widen the IPC surface beyond the local
user: the control pipe between `wabarc` and a running bar, and the tray hook pipe, are meant for the
local user only.

## User-built QML plugins

The config already runs code as the user, and a plugin does the same with fewer limits: a Qt QML
plugin is a native DLL that runs inside `wabar.exe` as soon as a shell imports its module. wabar
adds these places to the QML import path:

- `<config home>\modules`, when the folder exists;
- every folder given as `wabar.exe --import-path <dir>` (repeatable).

`--safe` adds neither, so recovery does not depend on a plugin.

What the guard does and does not do (`src/host/plugin_guard.*`):

- Before a folder reaches the engine, wabar reads each plugin named by a `qmldir` file under it. It
  refuses the whole folder, with one log line per plugin, when the plugin is not a Windows library
  for this processor, was built with another Qt major or minor version, or is a debug build in a
  release wabar (or the reverse). The file is read, not loaded.
- The guard prevents a crash from a mismatched build. It is not a trust check: it does not verify a
  signature, an author or the plugin's behaviour, and it does not sandbox the plugin.
- Put only plugins you built or trust in these folders, and keep `<config home>` writable by you
  alone. Anyone who can write there can run code as you, as they can with `shell.qml`.

The surface this adds is the import path and the DLLs under it; no network, pipe or registry entry
is added.

## Reporting a vulnerability

This repository is not published. Report issues privately to the maintainer, Jan De Dobbeleer,
rather than in a shared channel.
