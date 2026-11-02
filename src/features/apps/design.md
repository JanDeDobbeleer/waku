# apps

The app catalogue for a launcher, in Quickshell's shape so a ported launcher (caelestia) loads.
Plan: `docs/plans/2026-10-29-waku-extensibility-proposal.md` section 4.4 (the `apps` row) and
section 5 (Start menu, Store apps, icons); `docs/plans/2026-10-30-caelestia-port-assessment.md`.

## What it gives QML

| Where | Name | What |
|---|---|---|
| `import Quickshell` | `DesktopEntries` | singleton: `applications` (a model of `DesktopEntry`, role `modelData`, plus `values`, a JS array, and `count`), `byId(id)`, `heuristicLookup(name)` |
| `import Quickshell` | `DesktopEntry` | `id`, `name`, `genericName`, `comment`, `icon`, `execString`, `command`, `workingDirectory`, `startupClass`, `categories`, `keywords`, `noDisplay`, `runInTerminal`, `isApplication`, `actions`, `execute()`; waku adds `isStoreApp`, `aumid` |
| `import Quickshell` | `DesktopAction` | `id`, `name`, `icon`, `execString`, `command`, `execute()` |
| `import waku.services` | `AppsService` | the C++ singleton behind all of it, plus the waku form below |

`AppsService` (waku form): `applications`, `ready`, `loading`, `count`, `scanMs`; `search(query,
limit)` (ranked `DesktopEntry` list), `launch(id)`, `launchAsAdmin(id)`, `openLocation(id)`,
`refresh()`, `byId`, `heuristicLookup`.

`DesktopEntries.qml` (in `src/quickshell/core/qml`) is a QML singleton that forwards to
`AppsService`, because the host unit cannot include a feature's headers (rule S2). `DesktopEntry`
and `DesktopAction` are `waku.services` types. The `Quickshell` module registers them again under
the same names (`src/quickshell/core/qml/quickshell_types_core.h`) and does not re-export `waku.services`, so
`import Quickshell` shows no other `waku.services` name. A launcher that declares
`required property DesktopEntry modelData` therefore compiles.

## Sources

- Start Menu shortcuts: `.lnk` files below `FOLDERID_Programs` then `FOLDERID_CommonPrograms`,
  recursively, read with `IShellLink` (documented; `win32/start_menu_win.cpp`). The id is the path
  below the folder without `.lnk` ("Dev Tools/Beta"); a shortcut in both folders counts once, the
  user's first. Skipped: names with "uninstall" or "readme" or the word "help", hidden shortcuts,
  shortcuts that fail to load. The subfolders are the `categories`; the target's file name is a
  `keyword` and the `startupClass`.
- Store apps: `shell:AppsFolder` through `SHGetKnownFolderItem(FOLDERID_AppsFolder)` and
  `IEnumShellItems` (documented; `win32/apps_folder_win.cpp`). The folder lists desktop apps too;
  only items whose parsing name has a "!" (an AUMID, `<PackageFamilyName>!<AppId>`) are kept, and a
  Store app whose name a shortcut already has is dropped.
- No undocumented API anywhere in the feature, so no allowlist row.

Qt quirk: `QDirIterator` lists a Windows `.lnk` as a link, and only with `QDir::System`; without
it a Start Menu scan finds nothing. It also ignores the hidden attribute for them, so the real
resolver rejects hidden shortcuts.

## Launching

`win32/launch_win.cpp`. Shortcuts: `ShellExecuteEx` on the `.lnk` (verb `runas` for admin).
Store apps: `kernel::activateAppByAumid`, then `ShellExecuteEx` on `shell:AppsFolder\<aumid>` when
no window answers. Admin for a Store app goes through its execution alias: the only `.exe` in
`%LOCALAPPDATA%\Microsoft\WindowsApps\<PackageFamilyName>` (`findExecutionAlias`, read when the
Store apps are listed and kept in `AppRecord::elevationAlias`), started with `runas`. A package
with no alias or several has none, so `launchAsAdmin` returns false and the entry has no admin
action. `openLocation` runs `explorer.exe /select,<target>`; false for Store apps. `execute()` on
an entry and its actions ("Run as administrator": shortcuts and Store apps with an alias; "Open
file location": shortcuts only) go through the same path.
`runInTerminal` is always false: a console program opens its own window.

`launch` and `launchAsAdmin` record a launch in the frecency file.

## Search and frecency

`rank.h` is a pure function. Tiers: exact (1000), prefix (800..900, shorter names higher), word
start (600..700, earlier words higher), all query words start some word (500), subsequence
(200..300, consecutive runs higher). A keyword or generic-name match counts half. Ties go to
frecency, then name. A blank query lists the most used apps first.

`frecency.h`: a launch count that halves every 14 days, in `%LOCALAPPDATA%\waku\apps_frecency.json`
(`appDataDir()`; at most 500 entries). Written after every successful launch. Not written when the
desktop is not live.

## Threads and lifetime

- The scan runs on the global `QThreadPool`: shortcuts first (posted at once, `loading` stays
  true), then `shell:AppsFolder`, then the merged catalogue. A scan that is asked for during a scan
  runs after it. The destructor does not wait: the worker posts through a mutex-guarded owner
  pointer that the destructor clears.
- `DesktopEntry` objects are reused by id across rescans, so a QML reference survives; `changed`
  fires when a field changes. Removed apps are `deleteLater`ed.
- `QFileSystemWatcher` watches the Start Menu folders and all their subfolders and rescans after
  1.5 s of quiet. The real service keeps the watcher and the last complete catalogue in a
  process-wide `AppCatalog` (a `kernel::LingeringHub`): the service a reload builds starts from that
  catalogue without a scan unless a folder changed, and the watcher is updated by difference, not
  registered on every folder again. A rescan posts only its complete result; the shortcuts alone
  come first only while the list is empty.
- `image://appicon/<percent-encoded id>`: `QQuickAsyncImageProvider` with its own four-thread pool;
  each request calls `kernel::shellItemIcon` (documented `IShellItemImageFactory`, COM per call)
  and scales down to the request's `sourceSize` (64 when unset, at most 256). The provider reads
  the id to icon-source map the service refreshes after each scan. `Quickshell.iconPath(entry.icon)`
  returns the URL unchanged because it already contains `://`.
  The shell icon helper moved from the taskbar to `kernel/win32/shell_icons.*` (users: taskbar,
  apps).

## Tests and fakes

`Deps` takes the folder list, the shortcut resolver, the `shell:AppsFolder` enumerator, the
launcher, the icon loader and the frecency path. The tests fill a temp folder with empty `.lnk`
files and a fake resolver, and a fake enumerator; nothing starts an app. One test writes a real
`.lnk` with `IShellLink` and reads it back with the real resolver. The real constructor reads the
real folders only when `kernel::liveDesktop("apps")`; on the offscreen platform it is empty.

## Spike S5: scan cost on a real machine

Measured read-only with a throwaway program that links the feature's `scanShortcuts`, the real
resolver and the real enumerator (Windows 11 build 26300, ARM64, debug build, user with 156
shortcuts):

| Run | Shortcuts (156) | shell:AppsFolder (51 packaged apps) | Merge |
|---|---|---|---|
| 1 (cold) | 1469 ms | 842 ms | 5 ms |
| 2 | 535 ms | 596 ms | 9 ms |
| 3 | 731 ms | 638 ms | 10 ms |

The catalogue has 206 entries (156 + 51, minus one duplicate name). A full scan takes 1.1 to 2.3 s
in a debug build, all off the GUI thread; roughly 3 to 9 ms per shortcut, dominated by
`CoCreateInstance` and `IPersistFile::Load`. The launcher can show the shortcuts about 0.5 to 1.5 s
after start, the Store apps follow. If a release build is not clearly faster, the shortcut
resolution can be spread over a few threads.

## Open points

- A generic icon for apps whose icon fails to load (the request fails and the `Image` shows its
  error state).
- Win32 desktop apps without a Start Menu shortcut do not appear.
- `Quickshell.iconPath("firefox")` style theme names are not apps; only ids from the catalogue use
  `image://appicon`.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- `waku.services.AppsService` (`src/features/apps/qml/apps_service.{h,cpp}`): the one app catalogue of a `waku` run (Start Menu shortcuts and Store apps as `DesktopEntry` objects, ranked search, launch, frecency in `%LOCALAPPDATA%\waku\apps_frecency.json`), shared by every widget and by `DesktopEntries` of the `Quickshell` module; created by the QML engine on first use and destroyed with it (`create()` registers `image://appicon`). A `QFileSystemWatcher` on the Start Menu folders rescans after 1.5 s of quiet. Empty and inert unless `kernel::liveDesktop("apps")`
