# Unit tests of wabar.services and of the wabar module (Config, Bar, BarLayout, BarHost), contributed
# to the one test binary wabar_tests that tests/CMakeLists.txt defines.
wabar_contribute(
  TESTS
    test_audio.cpp
    test_bluetooth.cpp
    test_notifications.cpp
    test_control_center.cpp
    test_brightness_policy.cpp
    test_platform.cpp
  TEST_LIBS nlohmann_json::nlohmann_json
)
