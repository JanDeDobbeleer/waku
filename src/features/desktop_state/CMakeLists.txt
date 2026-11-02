# desktop_state: lock keys, keyboard layout and idle time (DesktopState), and IdleInhibitor.
# user32: GetKeyState, GetKeyboardLayout, GetLastInputInfo; kernel32 holds the rest.
waku_feature(NAME desktop_state
  SOURCES
    desktop_probe.h
  WIN32_SOURCES
    win32/desktop_probe_win.cpp
  QML_SOURCES
    qml/desktop_state.h
    qml/desktop_state.cpp
    qml/idle_inhibitor.h
    qml/idle_inhibitor.cpp
  TESTS
    tests/test_desktop_state.cpp
  SYSTEM_LIBS user32)
