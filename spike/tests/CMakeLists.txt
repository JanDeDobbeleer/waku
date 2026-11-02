# Unit tests of wabar.services and of the wabar module (Config, Bar, BarLayout, BarHost), contributed
# to the one test binary wabar_tests that tests/CMakeLists.txt defines.
wabar_contribute(
  TESTS
    test_control_center.cpp
  TEST_LIBS nlohmann_json::nlohmann_json
)
