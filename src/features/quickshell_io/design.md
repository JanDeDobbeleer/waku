# quickshell_io

Processes, files and launches for QML. Two parts:

- QML written for Quickshell (`import Quickshell.Io`, the `Quickshell` singleton) loads on wabar.
  Names, properties, signals and functions follow the Quickshell v0.3.0 documentation and how
  caelestia uses them. Plan: `docs/plans/2026-10-30-caelestia-port-assessment.md` section 5.3.
- wabar's own `Shell` singleton and `ShellProcess`, which the built-in widgets use (see "wabar's
  launches" below). They were the feature `shell` until it was folded in here, so that one feature
  owns every process wabar starts for QML.

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
| `Shell` | `Shell` | wabar's singleton, see below |
| `ShellProcess` | `ShellProcess` | wabar's interval runner, see below |

## wabar's launches

- `Shell` (`qml/shell_service.*`, singleton): `start(program, args)`, `command(line)`,
  `open(target)`, `env(name)`, `homeDir`, `configHome`. A widget calls it on click. Launches are
  detached and hidden (no console window); `open` runs ShellExecute on a worker thread, so the GUI
  thread never blocks. The three launches live in `Shell::Launcher`, which tests replace so no real
  program starts.
- `ShellProcess` (`qml/shell_process.*`): runs a command hidden and exposes its output, error
  output and exit code. `run()` starts it once, `interval > 0` reruns it; a run is killed after
  `timeout`, and a running child is killed when the object is destroyed. Both kills take the
  child's descendants too: each run gets its own `ProcessJob`, the same kill-tree job object as
  `Process`, released when the run ends normally. The QML name `Process` belongs to Quickshell
  (`QsProcess`).

Both use the hidden-launch and shell-open helpers from `kernel/win32/shell_launch.h`. Widgets are
trusted code: `Shell` starts any program with the bar's privileges. The audio, bluetooth, network
and github features declare `DEPENDS quickshell_io` for their `Shell` clicks ("open settings",
"open URL").

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
member to this class, so the host unit does not include a feature header. `watchFiles` and
`workingDirectory` are copied both ways. `clipboardText` is an alias to a `ClipboardText` object:
the clipboard is read when a shell first reads the value and cached until the clipboard changes,
never when the singleton is created. Creation happens on every start and reload, and a read can
block the GUI thread for seconds while another process holds the clipboard (Qt retries
`OleGetClipboard` with sleeps; an owner that renders on demand keeps `GetClipboardData` waiting).

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
`docs/architecture.md`). `Shell::open` hands ShellExecute to a worker owned by the kernel helper. No new Windows API outside the documented job-object calls
(`win32/process_job_win.cpp`).
