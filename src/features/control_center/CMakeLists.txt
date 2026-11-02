# control_center: the system side of the ControlCenter widget. Do Not Disturb (read through
# notifications), the app theme, the touch keyboard, power plans and modes, the user's name and
# picture, monitor brightness (through brightness) and the session and power actions behind a
# Launcher seam. Volume, microphone and the player come from audio and media.
waku_feature(NAME control_center
  DEPENDS audio media notifications brightness
  SOURCES
    app_theme.h
    power.h
    touch_keyboard.h
    user_info.h
  WIN32_SOURCES
    win32/app_theme_win.cpp
    win32/power_win.cpp
    win32/touch_keyboard_win.cpp
    win32/user_info_win.cpp
  QML_SOURCES
    qml/control_center_service.h
    qml/control_center_service.cpp
  QML_FILES
    qml/ControlCenter.qml
  TESTS
    tests/test_control_center.cpp
  SYSTEM_LIBS advapi32 ole32 powrprof secur32 shell32 user32)
