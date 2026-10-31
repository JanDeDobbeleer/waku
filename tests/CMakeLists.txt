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
  integration/test_plugin_import.cpp
  integration/test_qt_imports.cpp
  integration/test_font_engine.cpp
  integration/test_gallery_example.cpp
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
  wabar_services wabar_servicesplugin wabar_quickshell_services
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
          --qmldir "${PROJECT_SOURCE_DIR}/cmake/deploy_imports"
          --dir "$<TARGET_FILE_DIR:wabar_tests>" "$<TARGET_FILE:wabar_tests>"
  VERBATIM)

# One ctest test per gtest suite (ctest -R Tray runs the Tray* suites, each in one process), read
# from the TEST/TEST_F/TEST_P lines of the sources at configure time; an edited test source
# re-runs the configure step. A suite runs all its cases in one process with one QGuiApplication,
# which is what `wabar_tests --gtest_filter=Tray*` does too. Single cases: --gtest_filter.
# -DWABAR_TEST_PER_CASE=ON restores one ctest test (and process) per case through gtest discovery.
option(WABAR_TEST_PER_CASE "Register one ctest test per gtest case instead of per suite" OFF)
if(WABAR_TEST_PER_CASE)
  gtest_discover_tests(wabar_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
else()
  get_target_property(wabar_test_files wabar_tests SOURCES)
  set(wabar_test_suites "")
  foreach(file IN LISTS wabar_test_files)
    cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    if(NOT file MATCHES "\\.cpp$")
      continue()
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    file(STRINGS "${file}" lines REGEX "^(TEST|TEST_F|TEST_P)\\([A-Za-z0-9_]+,")
    foreach(line IN LISTS lines)
      string(REGEX REPLACE "^TEST(_F|_P)?\\(([A-Za-z0-9_]+),.*$" "\\2" suite "${line}")
      list(APPEND wabar_test_suites "${suite}")
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES wabar_test_suites)
  list(SORT wabar_test_suites)
  foreach(suite IN LISTS wabar_test_suites)
    # `Suite.*` for plain suites, `*/Suite.*` for the instantiations of a TEST_P suite.
    add_test(NAME ${suite} COMMAND wabar_tests "--gtest_filter=${suite}.*:*/${suite}.*")
    # A suite holds many cases: the ctest --timeout (120 s) is per case in spirit, so allow more.
    set_tests_properties(${suite} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)
  endforeach()
endif()
