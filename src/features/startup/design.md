# startup

`wakuc startup enable|disable|status` starts waku at sign-in through a per-user Scheduled Task. A
second task (`waku keys`) starts the keys daemon `wakukeys.exe`.

## Decisions

- Scheduled Task, not the Startup folder. Explorer delays Startup folder items by about 60 s. A
  logon task with no delay starts waku as soon as the session is up.
- Not the `Run` registry key: it has the same Explorer delay and no control over priority, run
  level or instances.
- Not an MSIX `StartupTask`: it needs a manifest extension and a packaged build, and its state
  lives in a per-package setting the user can flip in Settings. A task works for any install.
- The action is the bare `waku.exe`. The MSIX package puts a `waku.exe` execution alias on the user
  PATH, and the task resolves it at logon. The command registers nothing when `waku.exe` is not on
  the PATH, and says which folder to add. When the PATH finds another install than the one next
  to `wakuc`, the command warns and registers anyway: the task starts the one on the PATH.
- Defaults: no flags and no `--config-home`. waku resolves its own
  config home at start. `--config-home` is passed only when the user gave one (made absolute).
  `--no-app-bar` and `--keep-taskbar` drop the defaults. Extra waku flags follow `--`; an exact
  duplicate of a flag already present is dropped.
- waku never starts another tool. Start those with their own tasks or from the shell.
- The task: trigger at logon of the current user with no delay, Limited run level, priority 4, no
  execution time limit, one instance, runs on battery. `enable` twice replaces the task.
- An old `waku.lnk` in the Startup folder would start a second waku. `enable` deletes it and keeps
  no backup: a backup file in the Startup folder makes Windows ask which app opens it at sign-in.
- The keys daemon task. `enable` registers it after the bar task, with the same trigger and
  settings. The action is the bare `wakukeys.exe` (PATH, like `waku.exe`; the MSIX has an alias)
  with the `--config-home` the user gave (no `--config`: wakukeys reads `keys.conf` in that config
  home, else in `%USERPROFILE%\.config\waku`). It is registered only when that file exists: a user without a keys file gets one line saying so
  and no task. When `wakukeys.exe` is not on the PATH the command warns and skips the task; the bar
  task stays registered and the exit code stays 0. A registration error of the keys task fails the
  command. `disable` removes both tasks, `status` reports both. There is no flag to leave the
  keys task out: delete or never create `keys.conf`.
- The logic (`startup_service.*`) runs against the `StartupSystem` interface. The Windows
  implementation (`win32/startup_system_win.*`) uses the documented Task Scheduler COM API. Tests
  use `testing/fake_startup_system.h`.

## Open points

- An MSIX `StartupTask` as an option for packaged installs.
- A change to the PATH reaches new sessions only. After the user adds the folder, they sign out and
  in again before `enable` can find `waku.exe`.
