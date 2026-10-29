# shell

Run programs and open targets from QML. Two QML types, no Windows code of its own.

- `Shell` (`qml/shell_service.*`, singleton): `start(program, args)`, `command(line)`, `open(target)`,
  `env(name)`, `homeDir`, `configHome`. A widget calls it on click. Launches are detached and hidden
  (no console window); `open` runs ShellExecute on a worker thread, so the GUI thread never blocks.
  The three launches live in `Shell::Launcher`, which tests replace so no real program starts.
- `Process` (`qml/child_process.*`): runs a command hidden and exposes its output, error output and
  exit code. `run()` starts it once, `interval > 0` reruns it; a run is killed after `timeout`, and a
  running child is killed when the object is destroyed.

Both use the hidden-launch and shell-open helpers from `kernel/win32/shell_launch.h`; the feature has
no `win32/` folder. Widgets are trusted code: `Shell` starts any program with the bar's privileges.

## Dependencies

`DEPENDS` none. The audio, bluetooth, network and github widgets use `Shell` from QML ("open
settings" and "open URL" clicks), which is a QML-level dependency only.

## Threads

GUI thread only. `Shell::open` hands ShellExecute to a worker owned by the kernel helper.
