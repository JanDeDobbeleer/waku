# brightness: monitor brightness over DDC/CI and the power scheme, with its worker thread.
# Used by control_center (through the BrightnessControl interface in brightness.h).
waku_feature(NAME brightness
  SOURCES
    brightness.h
    brightness_policy.h
  WIN32_SOURCES
    win32/brightness_win.cpp
  TESTS
    tests/test_brightness_policy.cpp
  SYSTEM_LIBS dxva2 powrprof user32 uuid windowsapp)
