# audio: output and input volume, mute, default device; the Volume widget.
wabar_feature(NAME audio
  DEPENDS quickshell_io
  SOURCES
    audio_endpoints.h
    audio_stream_source.h
  WIN32_SOURCES
    win32/audio_endpoints_win.cpp
    win32/audio_streams_win.cpp
    win32/co_string.h
  QML_SOURCES
    qml/audio_service.h
    qml/audio_service.cpp
    qml/audio_streams.h
    qml/audio_streams.cpp
  QML_FILES
    qml/Volume.qml
  TESTS
    tests/test_audio.cpp
    tests/test_audio_streams.cpp
  SYSTEM_LIBS ole32 propsys uuid windowsapp
  WINRT_PCH winrt/base.h winrt/Windows.Foundation.h winrt/Windows.Foundation.Collections.h)
