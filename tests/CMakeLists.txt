# wabar_tests: the one test binary (proposal, section 8). Its sources are main.cpp here, the
# integration tests, and every TESTS contribution (wabar_feature(... TESTS ...) and
# wabar_contribute(TESTS ...) under src/). Added after src/ (top-level CMakeLists.txt).
# Select one feature by suite name: ctest -R Tray, or --gtest_filter=Tray*.
include(GoogleTest)
find_package(Qt6 REQUIRED COMPONENTS Test)

get_property(test_sources GLOBAL PROPERTY WABAR_TEST_SOURCES)
get_property(test_libs GLOBAL PROPERTY WABAR_TEST_LIBS)
list(REMOVE_DUPLICATES test_libs)

# tests/integration: tests that load built-in widgets of several features together (they may
# include anything); every unit's own tests come through the TESTS contributions.
add_executable(wabar_tests main.cpp
  integration/test_all_widgets_load.cpp
  integration/test_builtin_widgets.cpp
  integration/test_font_engine.cpp
  integration/test_quickshell_glue.cpp
  integration/test_safe_mode.cpp
  integration/test_vertical_panel.cpp
  integration/test_widget_popups.cpp
  ${test_sources})
# The control tests run wabarc.exe. The plugin tests load a plugin built here (tests/plugin).
add_subdirectory(plugin)
add_dependencies(wabar_tests wabarc wabar_test_module)
# The sample shells the integration tests load.
target_compile_definitions(wabar_tests PRIVATE WABAR_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples")
# The static QML plugins are linked so `import wabar` and `import Quickshell` resolve in the test
# engines.
target_link_libraries(wabar_tests PRIVATE
  wabar_options
  wabar_host wabar_hostplugin wabar_widgets wabar_widgetsplugin
  wabar_services wabar_servicesplugin
  wabar_quickshell wabar_quickshellplugin wabar_quickshell_wayland wabar_quickshell_waylandplugin
  ${test_libs}
  GTest::gtest Qt6::Test)
set_target_properties(wabar_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Qt DLLs, the offscreen platform plugin and the QtQuick QML modules the tests import sit next to
# the test exe so ctest runs without PATH. --qmldir only scans for Qt imports.
add_custom_command(TARGET wabar_tests POST_BUILD
  COMMAND Qt6::windeployqt $<IF:$<CONFIG:Debug>,--debug,--release>
          --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
          --include-plugins qoffscreen
          --qmldir "${PROJECT_SOURCE_DIR}/src/host/qml"
          --qmldir "${PROJECT_SOURCE_DIR}/src/app/wabar"
          --qmldir "${PROJECT_SOURCE_DIR}/src"
          --dir "$<TARGET_FILE_DIR:wabar_tests>" "$<TARGET_FILE:wabar_tests>"
  VERBATIM)

gtest_discover_tests(wabar_tests DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
