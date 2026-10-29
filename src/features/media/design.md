# media

Now playing: the system media sessions (GSMTC), their metadata, timeline and playback state, album
art, and the transport commands. One QML singleton (`Media`) and one widget (`Media.qml`).

## Layers

| Path | Role |
|---|---|
| `media_session.{h,cpp}` | UI-free: the value types (`MediaSessionData`, ...), the `MediaSessionSource` and `MediaSessionSink` interfaces, and `MediaSessionService`, which keeps the session list, picks the current session and interpolates the position on a GUI-thread timer while a session plays |
| `win32/media_session_winrt.cpp` | `SystemMediaSource`: every WinRT GSMTC object, on its own MTA thread |
| `win32/media_source_apps.cpp` | display name of the source app of a session (AppInfo, Start menu, process, AUMID text); the process, window and shell lookups are `kernel/win32/app_activation` |
| `qml/media_service.*` | `Media` (QML singleton), `imageMimeType`, `imageDataUrl`; re-emits the service's signals as one `changed`; album art as a data URL |
| `qml/Media.qml` | the widget, a pill with an optional popup |
| `testing/fake_media_source.h` | `FakeSource`, a source tests drive by hand |

## Threads

One `std::thread` (MTA) in `SystemMediaSource` owns the WinRT objects. WinRT handlers only enqueue a
task; the thread sleeps on a condition variable (no polling) and posts plain snapshots to the GUI
thread. `stop()` revokes the handlers, wakes the queue and joins. The source-app name cache in
`media_source_apps.cpp` is `QMutex`-guarded and written from that thread only.

## Dependencies

`DEPENDS` none. Windows import library: `windowsapp`. The control center will list `media` in its own
`DEPENDS` because it uses the `Media` QML type.
