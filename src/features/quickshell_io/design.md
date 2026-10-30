# quickshell_io

QML written for Quickshell (`import Quickshell.Io`, the `Quickshell` singleton) loads on wabar.
Names, properties, signals and functions follow the Quickshell v0.3.0 documentation and how
caelestia uses them. Plan: `docs/plans/2026-10-30-caelestia-port-assessment.md` section 5.3.

## Types

All C++ types are compiled into the QML module `wabar.services` (the feature uses `QML_SOURCES`).
`Quickshell.Io` is a forwarding module over it (see "Modules").

| QML name | Class | Notes |
|---|---|---|
| `Process` | `QsProcess` | `command`, `running` (true starts, false kills the tree), `environment`, `clearEnvironment`, `workingDirectory`, `stdinEnabled`, `processId` (and `pid`), `stdout`/`stderr`, `exec()`, `startDetached()`, `signal()`, `write()`, `started()`, `exited(exitCode, exitStatus)` |
| `StdioCollector` | same | `text`, `data`, `waitForEnd` (default true), `streamFinished()` |
| `SplitParser` | same | `splitMarker` (default `\n`), `read(data)` |
| `DataStreamParser` | same | uncreatable base with `read(data)` |
| `FileView` | `FileView` | `path`, `loaded`, `blockLoading`, `blockAllReads`, `blockWrites`, `preload`, `watchChanges`, `printErrors`, `atomicWrites`, `adapter` (default property); `text()`, `data()`, `reload()`, `setText()`, `setData()`, `writeAdapter()`, `waitForJob()`; signals `loaded`, `loadFailed(error)`, `saved`, `saveFailed(error)`, `adapterUpdated`, `fileChanged` |
| `FileViewError` | `FileViewError` | `Success`, `Unknown`, `FileNotFound`, `PermissionDenied`, `NotAFile` |
| `JsonAdapter`, `JsonObject` | same | QML-declared properties of type int, bool, string, real, var, list<string>, `JsonObject` are the JSON keys |
| `ElapsedTimer` | same | `elapsed()`, `elapsedMs()`, `elapsedNs()`, `restart()`, `restartMs()`, `restartNs()` (seconds are real numbers) |
| `QuickshellSingleton` | same | the window-free members of Quickshell's `Quickshell` singleton |

The earlier interval-based `Process` of the `shell` feature is now `ShellProcess` (same class, same
properties); the name `Process` belongs to Quickshell.

## Behaviour that differs from Linux

- `running = false` and `signal(2|3|9|15)` kill the process and its descendants (a Windows job
  object per started process, `KILL_ON_JOB_CLOSE`, so destroying the object kills the tree too).
  Other signal numbers do nothing. A child that exits normally does not take its background
  children with it.
- No parser on `stdout`/`stderr` means the stream goes to NUL. `stdinEnabled: false` closes stdin
  at start.
- `SplitParser` with the `\n` marker drops a trailing `\r`, because Windows tools end lines with
  `\r\n`. A tail without a marker is emitted when the stream ends.
- `FileView` creates missing parent folders on write. A `FileView` writes atomically by default
  (a temporary file, then rename). `watchChanges` watches the file and its folder, debounces 50 ms,
  re-reads on a thread-pool thread and emits `fileChanged()` only when the content differs from
  what the view holds, so a view's own writes are silent.
- `Quickshell.stateDir`, `cacheDir`, `dataDir` are `%LOCALAPPDATA%\wabar\{state,cache,data}`;
  `shellDir` is the config home. Nothing is created until a `FileView` writes there.

## Quickshell singleton: members and the wrapper

Implemented: `env`, `execDetached` (list or object), `processId`, `clipboardText`, `shellDir`,
`shellPath`, `configPath`, `configDir`, `shellRoot`, `dataDir`/`dataPath`, `stateDir`/`statePath`,
`cacheDir`/`cachePath`, `workingDirectory`, `watchFiles` (stored only), `hasThemeIcon`,
`hasVersion` (emulates 0.3), `hasQtVersion`, `inhibitReloadPopup` (no-op), `reload(hard)` and the
signals `reloadCompleted`, `reloadFailed`, `lastWindowClosed`.

Stubbed: `iconPath(icon, check|fallback)`. There is no icon-theme provider, so it returns the file
path or URL as is, `image://icon/<name>` when the Qt theme has the icon, `""` for `check == true`
and an unknown icon, the fallback string, or the name. TODO: an `image://icon/` provider over shell
icons.

Not here: `screens` (the host's ScreenList).

`reload(hard)` emits `reloadRequested(hard)`. The composition root joins it to the shell host
(`src/app/wabar/quickshell_glue.h`): it calls `setShellDir()` with the config home, queues a
`ShellHost::reload()` for each request and emits `reloadCompleted()` or `reloadFailed()` after
each reload.

**The `Quickshell` name.** `import Quickshell` gives a QML singleton, `Quickshell.qml` in
`src/host/quickshell/qml/`. It reads `screens` from the host's ScreenList and forwards every other
member to this class, so the host unit does not include a feature header. Writable members
(`clipboardText`, `watchFiles`, `workingDirectory`) are copied both ways.

## Modules

Types live in `wabar.services`. `Quickshell.Io` is a module of its own that registers its types
again under Quickshell's names (`src/app/wabar/quickshell_types_io.h`, QML_FOREIGN; assembled in
`src/modules/qml_modules.cmake`). It depends on `wabar.services` but does not re-export it: a qmldir
`import` line would show every wabar service name, and a shell's own `Workspaces.qml` or
`Wallpapers.qml` would lose to wabar's service of the same name. `FileViewError` is declared again
in that header with the same values.

## Tests and fakes

`FileAccess` (`file_access.h`) is the disk seam: the real one uses `QFile`/`QSaveFile`, tests install
a fake disk with injected errors. `QuickshellSingleton` takes its folders and a launcher in a test
constructor. Process tests run `cmd.exe`, `findstr.exe` and `ping.exe` in a temp folder.

## Threads

GUI thread, plus one `QThreadPool` job per `FileView` read or write (see the threading table in
`docs/architecture.md`). No new Windows API outside the documented job-object calls
(`win32/process_job_win.cpp`).
