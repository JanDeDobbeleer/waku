# media

Now playing: the system media sessions (GSMTC), their metadata, timeline and playback state, album
art, and the transport commands. One QML singleton (`Media`) and one widget (`Media.qml`).

## Layers

| Path | Role |
|---|---|
| `media_session.{h,cpp}` | UI-free: the value types (`MediaSessionData`, ...), the `MediaSessionSource` and `MediaSessionSink` interfaces, and `MediaSessionService`, which keeps the session list, picks the current session and interpolates the position on a GUI-thread timer while a session plays |
| `win32/media_session_winrt.cpp` | `SystemMediaSource`: every WinRT GSMTC object, on its own MTA thread |
| `win32/media_source_apps.cpp` | display name of the source app of a session (AppInfo, Start menu, process, AUMID text); the process, window and shell lookups are `kernel/win32/app_activation` |
| `qml/mpris.*` | `Mpris`, `MprisPlayer`, `MprisLoopState`, `MprisPlaybackState`: every session as a Quickshell.Services.Mpris player |
| `qml/media_service.*` | `Media` (QML singleton), `imageMimeType`, `imageDataUrl`; re-emits the service's signals as one `changed`; album art as a data URL |
| `qml/Media.qml` | the widget, a pill with an optional popup |
| `testing/fake_media_source.h` | `FakeSource`, a source tests drive by hand |

## Players (Quickshell.Services.Mpris)

`Mpris` is a QML singleton over every GSMTC session, for QML written against
`Quickshell.Services.Mpris` (caelestia `services/Players.qml`). `Media` and `Mpris` share one
`MediaSessionService` (`acquireMediaService()`), so GSMTC is opened once.

- `Mpris.players` is a model (role `modelData`) with a `values` list of `MprisPlayer`, in first-seen
  order. `Mpris.activePlayer` is the system's current session; assigning a player selects it.
- `MprisPlayer` properties: `uniqueId`, `identity` (source app name), `desktopEntry` (always empty),
  `dbusName` (the AUMID), `trackTitle`, `trackArtist`, `trackAlbum`, `trackArtUrl` (the thumbnail as a
  data URL), `metadata` (`xesam:*`, `mpris:artUrl`, `mpris:length`), `position` (seconds, settable
  seeks), `length`, `positionSupported`, `lengthSupported`, `isPlaying`, `playbackState`,
  `canControl`, `canPlay`, `canPause`, `canTogglePlaying`, `canGoNext`, `canGoPrevious`, `canSeek`,
  `shuffleSupported`, `shuffle` (settable), `loopSupported`, `loopState` (settable).
- Methods: `play()`, `pause()`, `togglePlaying()`, `stop()`, `next()`, `previous()`, `seek(offset)`.
  Commands go to the player's own session, not the current one.
- Signals: one `<property>Changed` per property, and `postTrackChanged()` after the title, artist or
  album changed.
- `canPlay`, `canPause` and `canTogglePlaying` all follow GSMTC's `IsPlayEnabled`.
- Position interpolation (100 ms timer) runs while the current session plays; other sessions get a
  fresh position on each of their events.
- The `Quickshell.Services.Mpris` module forwards to these types (`src/modules/qml_modules.cmake`).

## Threads

One `std::thread` (MTA) in `SystemMediaSource` owns the WinRT objects. WinRT handlers only enqueue a
task; the thread sleeps on a condition variable (no polling) and posts plain snapshots to the GUI
thread. `stop()` revokes the handlers, wakes the queue and joins. The source-app name cache in
`media_source_apps.cpp` is `QMutex`-guarded and written from that thread only.

## Dependencies

`DEPENDS` none. Windows import library: `windowsapp`. The control center will list `media` in its own
`DEPENDS` because it uses the `Media` QML type.
