# Security policy

## Threat model

wabar runs as the interactive user and loads that user's own QML config (`shell.qml`, `theme.qml`,
custom widgets) from `%USERPROFILE%\.config\wabar`. The config is code: it can run processes and read
files by design. Changes must neither sandbox the config nor widen the IPC surface beyond the local
user: the control pipe between `wabarc` and a running bar, and the tray hook pipe, are meant for the
local user only.

## Reporting a vulnerability

This repository is not published. Report issues privately to the maintainer, Jan De Dobbeleer,
rather than in a shared channel.
