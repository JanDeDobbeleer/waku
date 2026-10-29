# Unit tests of wabar.services and of the wabar module (Config, Bar, BarLayout, BarHost), contributed
# to the one test binary wabar_tests that tests/CMakeLists.txt defines.
wabar_contribute(
  TESTS
    test_audio.cpp
    test_builtin_widgets.cpp
    test_safe_mode.cpp
    test_tray_widget.cpp
    test_taskbar.cpp
    test_control_center.cpp
    test_github.cpp
    test_wallpapers.cpp
    test_wallpaper_control.cpp
  TEST_LIBS nlohmann_json::nlohmann_json
)
