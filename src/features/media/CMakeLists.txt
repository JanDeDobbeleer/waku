# Now playing through the system media transport controls (GSMTC): sessions, playback state, album
# art, source app names, and the Media widget. The Windows side is win32/ (WinRT); the app name
# lookups come from kernel/win32/app_activation.
wabar_feature(NAME media
  SOURCES
    media_session.cpp
    media_session.h
  WIN32_SOURCES
    win32/media_session_winrt.cpp
    win32/media_source_apps.cpp
  QML_SOURCES
    qml/media_service.cpp
    qml/media_service.h
    qml/mpris.cpp
    qml/mpris.h
  QML_FILES
    qml/Media.qml
  TESTS
    tests/test_media.cpp
    tests/test_mpris.cpp
  SYSTEM_LIBS windowsapp)
