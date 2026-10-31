# host: runtime notes

Mechanism notes for the shell runtime that do not fit a table row of `docs/architecture.md`. The rows there keep owner, work and stop.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- `Quickshell.Services.UPower.UPower`, `PowerProfiles`; `Quickshell.Services.Pipewire.Pipewire`; `Quickshell.Services.SystemTray.SystemTray`; `Quickshell.Bluetooth.Bluetooth`; `Quickshell.Hyprland.Hyprland` (and the enum singletons beside them) (`src/host/quickshell/{upower,pipewire,systemtray,bluetooth,hyprland}/*.qml`): QML singletons, as the Quickshell ones are. Each reads the wabar service singletons (`Battery`, `ControlCenter`, `AudioOutput`, `AudioInput`, `AudioStreams`, `Systray`, `Bluetooth`) through one replaceable `backend`/`service`/`output` property that tests set to a fake; none owns a thread or a native resource. Created by the engine on first use, GUI thread only

- `host::Hotkeys` (`src/host/hotkeys.{h,cpp}`): the global hotkeys of a `wabar` run, shared by every `GlobalShortcut`: it maps key combinations to callbacks (two shortcuts on one combination share one registration) and owns the one `HotkeyBackend`. A singleton because Windows scopes `RegisterHotKey` ids to the thread, and one table keeps the ids unique. GUI thread only; no thread of its own. The backend is built on the first `add`: `RegisterHotKey` (documented) with a null window and a native event filter on the GUI thread (`src/host/win32/hotkeys_win.cpp`); every id is unregistered when its last shortcut goes and, at the latest, in the destructor. Inert, no backend, when `kernel::liveDesktop("hotkeys")` is false (offscreen platform, `--no-hotkeys`). Tests install a fake (`src/host/testing/hotkeys_fixtures.h`)

- `wabar.Thumbnails` (`src/host/qml/thumbnails.{h,cpp}`, store `src/host/thumbnail_store.{h,cpp}`): `url(path, width)`, `prewarm(paths, width)` and `cancelPrewarm()` for a gallery of large images, and the owner of the `image://thumb/<path>?w=<px>` provider (registered by `create()`, as `AppsService` registers `appicon`). A singleton because QML needs an object to call and the provider belongs to the engine that creates it; it holds the shared `ThumbnailStore` (memory LRU of 64 MB, disk cache `%LOCALAPPDATA%\wabar	humbs`, keyed by SHA-1 of path, width, mtime and size, WebP q85 or PNG) and the prewarm pool. GUI thread only; created on first use. No disk cache, memory only, unless `kernel::liveDesktop("thumbnails")`
