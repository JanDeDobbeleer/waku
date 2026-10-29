# audio

Output and input volume, mute and default-device switching (Python `AudioOutputService` and
`AudioInputService`), and the Volume widget.

## Pieces

- `audio_endpoints.h`: `AudioEndpoints` (the seam), `AudioDevice`, `AudioFlow` and the factory
  `createSystemAudio`.
- `win32/audio_endpoints_win.cpp`: Core Audio implementation. The default-device switch uses the
  undocumented `IPolicyConfig` (allowlisted in `docs/architecture.md`).
- `qml/audio_service.*`: the `AudioOutput` and `AudioInput` QML singletons over one endpoint each.
- `qml/Volume.qml`: the widget; a right click opens the sound settings through `S.Shell`.
- `testing/fake_audio_endpoints.h`: `FakeEndpoints` for the tests.

## Threads

GUI thread only. Core Audio callbacks fire on COM threads and only re-post to the GUI thread, which
re-resolves the endpoint and emits. `stop()` releases the callbacks; the singleton's destructor
calls it.

## Depends on

kernel only in C++; `shell` for the QML `S.Shell.open` click.
