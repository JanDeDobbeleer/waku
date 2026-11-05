# audio

Output and input volume, mute and default-device switching (Python `AudioOutputService` and
`AudioInputService`), and the Volume widget.

## Pieces

- `audio_endpoints.h`: `AudioEndpoints` (the seam), `AudioDevice`, `AudioFlow` and the factory
  `createSystemAudio`.
- `win32/audio_endpoints_win.cpp`: Core Audio implementation. The default-device switch uses the
  undocumented `IPolicyConfig` (allowlisted in `docs/architecture.md`).
- `qml/audio_service.*`: the `AudioOutput` and `AudioInput` QML singletons over one endpoint each.
- `audio_stream_source.h`, `win32/audio_streams_win.cpp`, `qml/audio_streams.*`: per-app audio
  streams (below).
- `testing/fake_audio_streams.h`: `FakeStreams` for the tests.
- `qml/Volume.qml`: the widget; a right click opens the sound settings through `S.Shell`.
- `testing/fake_audio_endpoints.h`: `FakeEndpoints` for the tests.

## Per-app streams

`AudioStreams` (QML singleton) lists the audio sessions of the default output device, the apps in
the Windows volume mixer, for QML written against `Quickshell.Services.Pipewire` (celeste
`services/Audio.qml` reads `Audio.streams`, `stream.audio.volume`, `stream.audio.muted`,
`stream.properties["application.name"]`, `stream.description`).

- `AudioStreams.streams` is a model (role `modelData`) with a `values` list of `AudioStream`.
- `AudioStream`: `id` (session instance identifier), `name` (session display name, else the
  executable name), `description` (same), `icon` (session icon file, else the executable path),
  `pid`, `volume` (0..1, settable), `muted` (settable), `active` (rendering now), `isStream`,
  `ready`, `audio` (the stream itself, so `stream.audio.volume` works) and `properties`
  (`application.name`, `application.icon_name`, `application.process.id`).
- System sounds and expired sessions are left out. Inactive sessions stay in the list; filter on
  `active` to show only apps that are playing.
- Core Audio only, all documented: `IAudioSessionManager2` on the default render device,
  `IAudioSessionControl2`, `ISimpleAudioVolume`, plus `IAudioSessionNotification`,
  `IAudioSessionEvents` and `IMMNotificationClient` callbacks. The callbacks only re-post to the
  GUI thread; a session burst is read once after 40 ms. A default-device change rebuilds the list.
- Without a live desktop there is no source and the list stays empty.

## Threads

GUI thread only. Core Audio callbacks fire on COM threads and only re-post to the GUI thread, which
re-resolves the endpoint and emits. `stop()` releases the callbacks; the singleton's destructor
calls it.

## Depends on

kernel only in C++; `quickshell_io` for the QML `S.Shell.open` click.
