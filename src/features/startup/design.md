# startup

`wakuc startup enable|disable|status` starts waku at sign-in through a per-user Scheduled Task.

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
- Defaults: `--register-app-bar --hide-taskbar`, no `--config-home`. waku resolves its own
  config home at start. `--config-home` is passed only when the user gave one (made absolute).
  `--no-app-bar` and `--keep-taskbar` drop the defaults. Extra waku flags follow `--`; an exact
  duplicate of a flag already present is dropped.
- waku never starts komorebi or any other tool. Start those with their own tasks or from the shell.
- The task: trigger at logon of the current user with no delay, Limited run level, priority 4, no
  execution time limit, one instance, runs on battery. `enable` twice replaces the task.
- An old `waku.lnk` in the Startup folder would start a second waku. `enable` deletes it and keeps
  no backup: a backup file in the Startup folder makes Windows ask which app opens it at sign-in.
- The logic (`startup_service.*`) runs against the `StartupSystem` interface. The Windows
  implementation (`win32/startup_system_win.*`) uses the documented Task Scheduler COM API. Tests
  use `testing/fake_startup_system.h`.

## Open points

- An MSIX `StartupTask` as an option for packaged installs.
- A change to the PATH reaches new sessions only. After the user adds the folder, they sign out and
  in again before `enable` can find `waku.exe`.
