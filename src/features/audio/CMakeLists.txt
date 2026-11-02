# audio: output and input volume, mute, default device; the Volume widget.
wabar_feature(NAME audio
  DEPENDS shell
  SOURCES
    audio_endpoints.h
  WIN32_SOURCES
    win32/audio_endpoints_win.cpp
  QML_SOURCES
    qml/audio_service.h
    qml/audio_service.cpp
  QML_FILES
    qml/Volume.qml
  TESTS
    tests/test_audio.cpp
  SYSTEM_LIBS ole32 propsys uuid windowsapp)
