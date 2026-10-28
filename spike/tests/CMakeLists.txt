# Unit tests of barnacle.services and of the barnacle module (Config, Bar, BarLayout, BarHost).
# Widgets (QML) are out of scope.
include(GoogleTest)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(barnacle_services_tests
  test_main.cpp
  test_stat_sources.cpp
  test_process.cpp
  test_audio.cpp
  test_media.cpp
  test_wm_clients.cpp
  test_shell.cpp
  test_workspaces.cpp
  test_screen_match.cpp
  test_bar_host.cpp
  test_builtin_widgets.cpp
  test_theme.cpp
  test_popups.cpp
)
target_compile_definitions(barnacle_services_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# The static QML plugins are linked so `import barnacle` resolves in the test engines.
target_link_libraries(barnacle_services_tests PRIVATE
  barnacle_bar barnacle_barplugin barnacle_widgets barnacle_widgetsplugin
  barnacle_services barnacle_servicesplugin
  GTest::gtest Qt6::Test nlohmann_json::nlohmann_json)
set_target_properties(barnacle_services_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Qt DLLs, the offscreen platform plugin and the QtQuick QML modules the tests import sit next to
# the test exe so ctest runs without PATH.
add_custom_command(TARGET barnacle_services_tests POST_BUILD
  COMMAND Qt6::windeployqt $<IF:$<CONFIG:Debug>,--debug,--release>
          --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
          --include-plugins qoffscreen
          --qmldir "${CMAKE_SOURCE_DIR}/spike/bar"
          --qmldir "${CMAKE_SOURCE_DIR}/spike/widgets"
          --dir "$<TARGET_FILE_DIR:barnacle_services_tests>" "$<TARGET_FILE:barnacle_services_tests>"
  VERBATIM)

gtest_discover_tests(barnacle_services_tests DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
