# wabar Wallpapers widget: design note

Date: 2026-10-29. Branch `spike/qt-quick-bar`. Reference: Python YASB `wallpapers` widget and the
Qt Widgets port in `widgets/wallpapers/`.

## What it does

Python's wallpapers widget does four things: an optional bar button, a gallery of the wallpapers in
`image_path`, a hotkey that toggles the gallery, and an animated change (`engine`). wabar ships the
first three plus an optional random timer. The animation is deferred (see below).

The user's config (`label ""`, `image_path ~/.config/wallpapers`, gallery `strip`, portrait, 240 px,
accent `#fff`, radius 0, `alt+e` on the cursor screen) needs no bar button. The widget stays loaded
so the hotkey works.

## What is reused

No wallpaper logic is re-implemented. The `Wallpapers` singleton wraps the UI-free classes:

- `yasb::widgets::WallpaperManager`: set a wallpaper (`changeBackground(path)`), the random pick
  (never the same twice), the auto-change timer, post-change commands.
- `collectImageFiles` / `imagePathsFrom` (`wallpaper_files`): the folder scan in Explorer name order.
- `ThumbnailCache`: PNG thumbnails, keyed by file identity and size, under
  `%LOCALAPPDATA%\YASB\wallpaper_thumbnails` (shared with the Widgets build).
- `platform::DesktopWallpaperBackend` (`IDesktopWallpaper`): reads the current wallpaper and, through
  the manager, sets it. Tests give both a fake.
- `platform::foregroundMonitorRect()` for the `active` screen mode.

## QML-facing API (`wabar.services.Wallpapers`, singleton)

| Member | Meaning |
|---|---|
| `imagePath` (string, read/write) | folder or file the gallery lists; setting it rescans |
| `files` | list of maps `{path, name, thumbnail, current}` in name order |
| `current` | path of the wallpaper Windows shows (first monitor), empty if unknown |
| `available` | false when `IDesktopWallpaper` cannot be created (offscreen platform) |
| `galleryOpen`, `galleryScreen` | gallery state and the name of the screen it opens on |
| `setWallpaper(path)`, `random()` | change the wallpaper on every monitor |
| `toggleGallery()`, `closeGallery()` | open (on the screen the mode picks) or close |
| `configure(imagePath, hotkey, hotkeyScreen, autoChangeSeconds)` | what `Wallpapers.qml` passes in |
| `image://wallpaper/<path>?<stamp>` | thumbnail; `sourceSize` sets the size |

`Wallpapers.qml` takes `label`, `imagePath`, `hotkey`, `hotkeyScreen`, `autoChange` and the gallery
look (`imageWidth`, `orientation`, `cornerRadius`, `accentColor`). `WallpaperGallery.qml` is the
Qt Quick window.

## Decisions

1. **Gallery is a Qt Quick `Window`, not a port of `WallpaperGallery`.** The Widgets gallery paints
   projected cards by hand; a `ListView` does the strip with far less code. Only the `strip` type
   ships; `default`, `magnified` and `slide` fall back to it. A full-screen translucent window
   makes "click outside closes" a plain mouse area. Escape, Enter, arrows, losing focus close or move.
2. **Open on the cursor screen.** `hotkeyScreen` follows Python: `cursor`, `primary`, `active`
   (foreground window's monitor); anything else means primary.
3. **Hotkey: `RegisterHotKey` on the GUI thread.** The Widgets build's `HotkeyService` lives in
   `app/` (it needs the event bus and a thread), which `spike/` must not include. The service
   registers one hotkey with a null window handle and a `QAbstractNativeEventFilter` receives
   `WM_HOTKEY` in Qt's own message loop. No new thread and no hidden window. `RegisterHotKey` is
   documented, so no allowlist row. If the combination is taken the service logs a warning and the
   gallery stays reachable through the bar button. Never registered offscreen, in tests, or after
   `setDisabled(true)`.
4. **Thumbnails load asynchronously.** The provider has `ForceAsynchronousImageLoading`, so Qt
   decodes on its image-reader thread. The provider holds only the immutable cache object and reads
   files; it touches no service state.
5. **`current` comes from `IDesktopWallpaper`**, not from the last request, so a failed set shows.
6. **Auto-change** is the manager's timer (`autoChange` seconds, 0 off), cheap to expose. `random()`
   uses the manager's picker.
7. **Non-default constructor.** Qt prefers a singleton's default constructor over `create()`, and
   `create()` registers the image provider (as for Taskbar).

## Left out

- **The `split` transition (and every `engine` animation): deferred.** `WallpaperEngine` is a
  `QWidget` painted on a WorkerW child window; wabar runs a `QGuiApplication` with no Qt Widgets.
  Rewriting it as a Qt Quick overlay is a separate job. The manager is configured with the engine
  off, so a change is a plain `IDesktopWallpaper` call. No fake animation.
- `default`/`magnified`/`slide` gallery types, mouse-wheel/page-key scrolling, `run_after` commands,
  other keybinding actions (`change_wallpaper`), `system` accent colour, tooltips.
- A hotkey without a Wallpapers item: the hotkey belongs to the singleton, so it exists only once a
  `Wallpapers` item was created and called `configure`.
- Double-click to apply (Python): a single click applies, which is what the brief asks for.
