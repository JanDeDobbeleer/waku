# Unit tests of wabar.services and of the wabar module (Config, Bar, BarLayout, BarHost).
# Widgets (QML) are out of scope.
include(GoogleTest)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(wabar_services_tests
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
  test_control.cpp
  test_safe_mode.cpp
  test_tray_model.cpp
  test_systray.cpp
  test_tray_pins.cpp
  test_tray_widget.cpp
  test_active_layout.cpp
  test_bluetooth.cpp
  test_network.cpp
  test_notifications.cpp
  test_taskbar.cpp
  test_control_center.cpp
  test_github.cpp
  test_wallpapers.cpp
)
target_compile_definitions(wabar_services_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# The static QML plugins are linked so `import wabar` resolves in the test engines.
target_link_libraries(wabar_services_tests PRIVATE
  wabar_bar wabar_barplugin wabar_widgets wabar_widgetsplugin
  wabar_services wabar_servicesplugin
  GTest::gtest Qt6::Test nlohmann_json::nlohmann_json)
set_target_properties(wabar_services_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Qt DLLs, the offscreen platform plugin and the QtQuick QML modules the tests import sit next to
# the test exe so ctest runs without PATH.
add_custom_command(TARGET wabar_services_tests POST_BUILD
  COMMAND Qt6::windeployqt $<IF:$<CONFIG:Debug>,--debug,--release>
          --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
          --include-plugins qoffscreen
          --qmldir "${CMAKE_SOURCE_DIR}/spike/bar"
          --qmldir "${CMAKE_SOURCE_DIR}/spike/widgets"
          --dir "$<TARGET_FILE_DIR:wabar_services_tests>" "$<TARGET_FILE:wabar_services_tests>"
  VERBATIM)

gtest_discover_tests(wabar_services_tests DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
