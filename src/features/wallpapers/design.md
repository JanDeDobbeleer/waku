# waku Wallpapers widget: design note

Date: 2026-10-29. Reference: Python YASB `wallpapers` widget (the Qt Widgets port that preceded
waku is at the git tag `widgets-main-final`).

## What it does

Python's wallpapers widget does four things: an optional bar button, a gallery of the wallpapers in
`image_path`, a hotkey that toggles the gallery, and an animated change (`engine`). waku ships the
first three plus an optional random timer, and the animation (see "Transition engine").

The user's config (`label ""`, `image_path ~/.config/wallpapers`, gallery `strip`, portrait, 240 px,
accent `#fff`, radius 0, `alt+e` on the cursor screen) needs no bar button. The widget stays loaded
so the hotkey works.

## What is reused

No wallpaper logic is re-implemented. The `Wallpapers` singleton wraps the UI-free classes:

- `waku::wallpapers::WallpaperManager`: set a wallpaper (`changeBackground(path)`), the random pick
  (never the same twice), the auto-change timer, post-change commands.
- `collectImageFiles` / `imagePathsFrom` (`wallpaper_files`): the folder scan in Explorer name order.
- Thumbnails: the host's `image://thumb/` provider (`waku.Thumbnails`, one memory LRU and one
  WebP disk cache for the run). The gallery builds its URLs with `Thumbnails.url(path, width)`.
  The feature's own PNG cache and `image://wallpaper` provider are gone; the first start removes
  their folder `%LOCALAPPDATA%\waku\wallpaper_thumbnails`, which nothing pruned.
- `wallpapers::DesktopWallpaperBackend` (`desktop_wallpaper.h`, `IDesktopWallpaper`): reads the current wallpaper and, through
  the manager, sets it. Tests give both a fake.
- `wallpapers::foregroundMonitorRect()` (`win32/window_focus.h`) for the `active` screen mode.

## QML-facing API (`waku.services.Wallpapers`, singleton)

| Member | Meaning |
|---|---|
| `imagePath` (string, read/write) | folder or file the gallery lists; setting it rescans |
| `files` | list of maps `{path, name, current}` in name order; `filesChanged` only when the list changes |
| `current` | path of the wallpaper Windows shows (first monitor), empty if unknown |
| `available` | false when `IDesktopWallpaper` cannot be created (offscreen platform) |
| `galleryOpen`, `galleryScreen` | gallery state and the name of the screen it opens on |
| `setWallpaper(path)`, `random()` | change the wallpaper on every monitor |
| `toggleGallery()`, `closeGallery()` | open (on the screen the mode picks) or close |
| `configure(imagePath, hotkey, hotkeyScreen, autoChangeSeconds)` | what `Wallpapers.qml` passes in |
| `setLockScreen` (bool, read/write, default true) | a change also sets the Windows lock screen (and sign-in screen) picture; see "Lock screen" |

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
3. **Hotkey: a handle in `kernel::Hotkeys`.** The service adds its combination to the process-wide
   hotkey table that `GlobalShortcut` uses too (`src/kernel/qt/hotkeys.h`): `RegisterHotKey` with a
   null window on the GUI thread and one native event filter for `WM_HOTKEY`. No thread and no
   hidden window of its own. The feature had its own copy of the parser and the registration while
   the table lived in `host/`, which a feature must not include; it moved to the kernel so both
   share one id range, one filter and `--no-hotkeys`. A `GlobalShortcut` on the same combination
   shares the registration, and both fire. If another program owns the combination the table logs
   it as taken, the service logs a warning and the gallery stays reachable through the bar button
   and `wakuc wallpaper gallery`. Never registered offscreen, in tests (they install the kernel's
   fake backend), when `kernel::liveDesktop("hotkeys")` is false (`--no-hotkeys`), or when the
   service is unavailable (`kernel::liveDesktop("wallpapers")` false).
4. **Thumbnails come from the host.** `image://thumb/` decodes on its own pool with a scaled
   read and shares one memory and disk cache with every other gallery of the run. The service
   touches no image.
5. **`current` comes from `IDesktopWallpaper`**, not from the last request, so a failed set shows.
6. **Auto-change** is the manager's timer (`autoChange` seconds, 0 off), cheap to expose. `random()`
   uses the manager's picker. The timer calls the service's `random()`
   (`WallpaperManager::Dependencies::timedChange`), so a timed change is a manual one: the lock
   screen follows, a dry run only logs and the animation plays when `engine` is on.
7. **Non-default constructor.** Qt prefers a singleton's default constructor over `create()`, and
   `create()` registers the image provider (as for Taskbar).

## Lock screen

A wallpaper change also sets the same image as the Windows lock screen picture, which the sign-in
screen shows too. `setLockScreen` is on by default; a shell turns it off with
`Wallpapers.setLockScreen = false`.

- **API.** WinRT `LockScreen.SetImageFileAsync(StorageFile)` (`Windows.System.UserProfile`),
  documented, so no allowlist row. Windows copies the file.
- **Behind an interface.** `LockScreenSetter` (`lock_screen.h`); the real one is
  `win32/lock_screen_win.cpp`, tests use `testing/fake_lock_screen.h`. Off the live desktop the
  service has no setter, so tests and offscreen runs never touch the lock screen.
- **When.** After the wallpaper was set: `current` (read back from `IDesktopWallpaper`) must equal
  the new path, so a failed set leaves the lock screen alone. Gallery clicks, `random()`, `wakuc
  wallpaper ...`, the animated change and the auto-change timer (which calls `random()`) all
  qualify.
- **Dry run.** `WAKU_WALLPAPER_DRYRUN=1` or `--dry-run` never calls the API; the service logs
  `Wallpapers: lock screen (dry run): <path>`.
- **Skipped files.** Missing files, videos (by extension) and animated GIF or WebP files: one info
  line, no call.
- **Formats.** jpg, jpeg, png and bmp go as they are. Anything else (webp, tif, still gif) is
  decoded on the worker and written to `%LOCALAPPDATA%\waku\lock_screen.png` first.
- **Thread.** One worker thread, started by the first call, MTA; only the latest path is set. The
  GUI thread never waits on WinRT. A failure is one warning line. Group policy or an unpackaged
  process can make the call fail; it then fails soft.

## Transition engine (Qt Quick)

The animated change (`engine`, `animation`) is ported from the `QWidget` `WallpaperEngine` to Qt Quick,
so the waku wallpaper widget needs no Qt Widgets code. The old engine is in the Widgets build
(git tag `widgets-main-final`).

- **Approach.** `qml/wallpaper_transition.*`: `WallpaperTransition` owns a frameless,
  input-transparent `QQuickWindow` and one `WallpaperScene` (`qml/wallpaper_scene.*`, a plain
  `QQuickItem` with scene graph nodes, built in C++: no QML file) that shows, for every monitor
  area, the old image and then the new image revealed by the animation's shape at `progress`
  (0..1). A `QVariantAnimation` (1200 ms, OutQuad) drives `progress`. The window is
  re-parented to WorkerW (above the wallpaper, below the icons) and ends when the animation does.
  When it ends the service sets the wallpaper (one `IDesktopWallpaper` call, the existing path); the
  overlay lingers a moment so the desktop never shows the old picture, then goes. A new change
  during the animation cancels it (no commit for the cancelled one) and starts the next.
- **Scene graph, not a painted item.** The first version was a full-screen `QQuickPaintedItem`:
  every frame it rasterized both images into a CPU frame buffer (screen size, 4 bytes a pixel)
  that was then uploaded again. `WallpaperScene` uploads each laid-out image once
  (`createTextureFromImage`, one texture per distinct image, so a span layout shares) and a frame
  only rewrites vertices. Per monitor: an opaque textured quad for the old image (hidden once the
  reveal is complete); a flat-colour fill of the reveal shape (the desktop background, as the
  reference fills the clip); the new image as triangles of the reveal shape with texture
  coordinates, under a `QSGOpacityNode` that holds `t` for a fade and 1 for the shapes. Qt Quick has no
  path clip, so the shapes are convex polygons (`wallpaper_reveal.*`, UI-free: `revealPieces`,
  `computeAreaFrame`): the circle and the slide are a polygon whose edge stays within 0.2 px of
  the circle (16 to 512 segments), the diamond a quad, the split two rectangles, each clipped to
  the monitor and, for the new image, to the image's own rectangle (a "fit" layout leaves the
  background around it). No shader, no `.qsb`.
- **Reference.** `paintWallpaperFrame` stays: `renderFrame()` and the tests render through it.
  `test_wallpaper_reveal.cpp` checks that the polygons cover the same area and the same pixels as
  `revealPath` for every shape at several progress values and monitor sizes;
  `test_wallpaper_scene.cpp` checks the node tree (textures per distinct image, one rebuild,
  geometry per frame) on the software scene graph, which builds the tree but draws no triangles.
- **Threads of the scene.** The GUI thread passes the images (they share the pixels of the
  transition's pixmaps) and the progress. `updatePaintNode` runs on the render thread with the GUI
  thread blocked; the textures and nodes live there and go with the node. It catches every
  exception and logs. A lost scene graph rebuilds from the same images.
- **Edge quality.** The reveal edge is antialiased only if the window's surface is multisampled;
  the overlay window asks for no multisampling (a 4x swap chain costs 4x its memory), so the
  circle, diamond and slide edge are one hard pixel step, the split edges are pixel aligned.
- **Reused.** `wallpaper_layout` (`layoutForScreens`, `revealPath`, `readDesktopSettings`): the same
  per-monitor areas, fit modes (fill, fit, stretch, tile, center, span) and reveal shapes as the old
  engine. `attachToWorkerW` and `transcodedWallpaperPath` (`win32/workerw.h`) for the Win32 side: no Win32 is
  duplicated and no undocumented API is added.
- **Threads.** GUI thread. One short-lived `QThread` decodes the new image and the old one (the
  transcoded wallpaper); it touches no Qt Quick object and is joined before the transition is
  destroyed. Scaling for the monitors (`QPixmap`) runs on the GUI thread, as before.
- **Failure.** WorkerW not found or an unreadable image: the window never shows, `skipped(reason)` is
  logged once and the wallpaper is set at once. Unknown animation names fall back to a cross-fade.
- **Dropped.** The 50 ms window fade-in before the reveal (the window sits at opacity 0.999 instead:
  a child of WorkerW is only composed on this Windows build when it is layered, and Qt layers a
  window once its opacity is not 1). The auto-change timer animates like the gallery and the
  right-click random, since it calls `random()`. Seams for tests: `WallpaperTransition::Platform`
  (settings, old image path, attach, decode, show) and `Wallpapers::Deps::transition`.
- **Dry run.** `WAKU_WALLPAPER_DRYRUN=1` plays the transition and logs instead of calling
  `IDesktopWallpaper`, so it can be watched without changing the real wallpaper.

## Left out

- **The manager's own engine.** It stays off (it is a `QWidget`); the Qt Quick transition above replaces it.
- `default`/`magnified`/`slide` gallery types, mouse-wheel/page-key scrolling, `run_after` commands,
  other keybinding actions (`change_wallpaper`), `system` accent colour, tooltips.
- Double-click to apply (Python): a single click applies, which is what the brief asks for.

## No widget: `WallpaperSettings` and `wakuc wallpaper`

- **`WallpaperSettings`** (`waku.services`, `qml/wallpaper_settings.*`) is a non-visual QML
  type declared next to the windows: `ShellRoot { WallpaperSettings { imagePath: ...; hotkey: "alt+e";
  engine: true; animation: "split" } Panel { ... } }`. It has `imagePath`, `hotkey`, `hotkeyScreen`,
  `autoChange`, `engine`, `animation` and the gallery look (`imageWidth`, `orientation`,
  `cornerRadius`, `accentColor`). `ShellRoot` and `Scope` keep any object they declare; one that is not
  a window is a part.
- **Activation.** `ShellHost::install` calls `activate()` (found by name, so `waku` does not link
  `waku.services`) on every part once the config is live. Not in the validation pass: that runs in
  a throwaway engine and must not build a second service or grab the hotkey. `activate()` fetches
  the `Wallpapers` singleton of the engine (creating it), calls `configure` and `configureEngine`,
  and creates the gallery window (`WallpaperGallery.qml`, loaded by URL) as its child, so the hotkey
  and `wakuc wallpaper gallery` work with no widget. Destroying the object (a reload, quitting)
  switches the folder, hotkey and engine off; the next config's settings then switch them on.
- **Sharing.** The singleton is shared with any `Wallpapers` widget; the last `configure` wins. Use
  one or the other: both create a gallery and two would open at once.
- **CLI.** `wakuc wallpaper set|random|next|previous [--dry-run]` and `gallery`. The client sends one
  line; arguments with spaces are quoted by doubling (`"a ""b"" c"`, no backslash escapes, so a
  Windows path is left alone: `joinCommand`/`splitCommand` in `control/control.h`). A relative
  path with a folder part is made absolute by the client; a bare name is looked up in the folder.
  `answerWallpaperCommand` (`wallpaper_command.h`) parses `wallpaper <verb> [--dry-run] [arg]` and
  calls the `WallpaperControl` interface of the same header, which `Wallpapers` implements and
  tests fake. Neither `control` nor `host` knows the verb: `src/app/waku/main.cpp` adds it to the
  bar's control handler as a `host::ControlCommands` entry. The service is looked up when a
  command arrives, so a bar without wallpaper settings builds it only on the first command.
- **Answers.** `ok: <full path>` once the change has started (the animation may still run), `ok:
  gallery opened|closed`, or `error: <why>` with nothing changed. `set` checks that the target
  exists, is a regular file, has a supported extension (`supportedImageTypes`) and a header Qt can
  read. `next`/`previous` use the Explorer-ordered list and wrap; when the current wallpaper is not
  in the folder `next` is the first file and `previous` the last. `random` never picks the current
  one.
- **Dry run.** `--dry-run` is a per-call argument of the service (`Wallpapers::Deps::dryRun` and
  `WAKU_WALLPAPER_DRYRUN` still work): the animation plays and the setter is skipped. Because the
  wallpaper does not change, repeated `next --dry-run` shows the same file.
- **Left out.** A `WallpaperSettings` removed from `shell.qml` is switched off by the reload that drops it.

## Notes moved from docs/architecture.md

The thread and singleton tables there keep one line per cell. This is the longer text of their rows before they were shortened.

- `waku.services.Wallpapers` (`src/features/wallpapers/qml/wallpapers_service.{h,cpp}`): the wallpaper folder listing, current wallpaper, gallery state and toggle hotkey of a `waku` run, shared by every Wallpapers widget; created by the QML engine on first use and destroyed with it. Wraps the UI-free `WallpaperManager`, `collectImageFiles` and `DesktopWallpaperBackend` (animated `engine` off); thumbnails are the host's `image://thumb/`. No thread of its own: the hotkey is a handle in `kernel::Hotkeys` (GUI thread), removed in the destructor. Inert, no hotkey, when `kernel::liveDesktop("wallpapers")` is false; no hotkey either when `kernel::liveDesktop("hotkeys")` is false (`--no-hotkeys`).
