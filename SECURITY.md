# Security policy

## Threat model

yasb-native runs as the interactive user and reads that user's own `config.yaml` and `styles.css`.
The config is code: `exec` callbacks run shell commands through `cmd.exe /c` by design, exactly as
in Python YASB. The CSS preprocessor follows `@import` and `url()` paths anywhere on disk, which is
consistent with the config already being trusted. The command and log named pipes
(`\\.\pipe\yasb_pipe_cli`, `\\.\pipe\yasb_pipe_log`) are meant for the local user only; restricting
them to the current user's SID and rejecting remote clients is in progress (plan unit R5).
Changes must neither sandbox the config nor widen the IPC surface beyond the local user.

## Reporting a vulnerability

This repository is not published. Report issues privately to the maintainer, Jan De Dobbeleer,
rather than in a shared channel.
