# waku_tests: the one test binary (proposal, section 8). Its sources are main.cpp here, the
# integration tests, and every TESTS contribution (waku_feature(... TESTS ...) and
# waku_contribute(TESTS ...) under src/). Added after src/ (top-level CMakeLists.txt).
# Select one feature by suite name: ctest -R Tray, or --gtest_filter=Tray*.
include(GoogleTest)
find_package(Qt6 REQUIRED COMPONENTS Test)

get_property(test_sources GLOBAL PROPERTY WAKU_TEST_SOURCES)
get_property(test_libs GLOBAL PROPERTY WAKU_TEST_LIBS)
list(REMOVE_DUPLICATES test_libs)

# tests/integration: tests that load built-in widgets of several features together, or check that
# two units share one kernel object (test_hotkeys_shared.cpp); test_wakukeys_args.cpp covers the
# command line of wakukeys.exe, whose parser is compiled in here; they may include anything. Every
# unit's own tests come through the TESTS contributions.
add_executable(waku_tests main.cpp
  integration/test_all_widgets_load.cpp
  integration/test_builtin_widgets.cpp
  integration/test_plugin_import.cpp
  integration/test_qt_imports.cpp
  integration/test_font_engine.cpp
  integration/test_gallery_example.cpp
  integration/test_hotkeys_shared.cpp
  integration/test_quickshell_glue.cpp
  integration/test_safe_mode.cpp
  integration/test_vertical_panel.cpp
  integration/test_widget_popups.cpp
  integration/test_wakukeys_args.cpp
  ${PROJECT_SOURCE_DIR}/src/app/wakukeys/keys_args.cpp
  ${test_sources})
# The control tests run wakuc.exe. The plugin tests load a plugin built here (tests/plugin).
add_subdirectory(plugin)
add_dependencies(waku_tests wakuc wakukeys waku_test_module)
# The sample shells the integration tests load.
target_compile_definitions(waku_tests PRIVATE WAKU_EXAMPLES_DIR="${PROJECT_SOURCE_DIR}/examples"
  WAKU_DEFAULT_SHELL_QML="${PROJECT_SOURCE_DIR}/src/app/waku/DefaultShell.qml")
# The static QML plugins are linked so `import waku` and `import Quickshell` resolve in the test
# engines.
target_link_libraries(waku_tests PRIVATE
  waku_options
  waku_host waku_hostplugin waku_widgets waku_widgetsplugin
  waku_services waku_servicesplugin waku_quickshell_modules
  ${test_libs}
  GTest::gtest Qt6::Test)
set_target_properties(waku_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Qt DLLs, the offscreen platform plugin and the QtQuick QML modules the tests import sit next to
# the test exe so ctest runs without PATH. --qmldir only scans for Qt imports.
add_custom_command(TARGET waku_tests POST_BUILD
  COMMAND Qt6::windeployqt $<IF:$<CONFIG:Debug>,--debug,--release>
          --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
          --include-plugins qoffscreen
          --qmldir "${PROJECT_SOURCE_DIR}/src/host/qml"
          --qmldir "${PROJECT_SOURCE_DIR}/src/app/waku"
          --qmldir "${PROJECT_SOURCE_DIR}/src"
          --qmldir "${PROJECT_SOURCE_DIR}/cmake/deploy_imports"
          --dir "$<TARGET_FILE_DIR:waku_tests>" "$<TARGET_FILE:waku_tests>"
  VERBATIM)

# One ctest test per gtest suite (ctest -R Tray runs the Tray* suites, each in one process), read
# from the TEST/TEST_F/TEST_P lines of the sources at configure time; an edited test source
# re-runs the configure step. A suite runs all its cases in one process with one QGuiApplication,
# which is what `waku_tests --gtest_filter=Tray*` does too. Single cases: --gtest_filter.
# -DWAKU_TEST_PER_CASE=ON restores one ctest test (and process) per case through gtest discovery.
option(WAKU_TEST_PER_CASE "Register one ctest test per gtest case instead of per suite" OFF)
if(WAKU_TEST_PER_CASE)
  gtest_discover_tests(waku_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
else()
  get_target_property(waku_test_files waku_tests SOURCES)
  set(waku_test_suites "")
  foreach(file IN LISTS waku_test_files)
    cmake_path(ABSOLUTE_PATH file BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)
    if(NOT file MATCHES "\\.cpp$")
      continue()
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
    file(STRINGS "${file}" lines REGEX "^(TEST|TEST_F|TEST_P)\\([A-Za-z0-9_]+,")
    foreach(line IN LISTS lines)
      string(REGEX REPLACE "^TEST(_F|_P)?\\(([A-Za-z0-9_]+),.*$" "\\2" suite "${line}")
      list(APPEND waku_test_suites "${suite}")
    endforeach()
  endforeach()
  list(REMOVE_DUPLICATES waku_test_suites)
  list(SORT waku_test_suites)
  foreach(suite IN LISTS waku_test_suites)
    # `Suite.*` for plain suites, `*/Suite.*` for the instantiations of a TEST_P suite.
    add_test(NAME ${suite} COMMAND waku_tests "--gtest_filter=${suite}.*:*/${suite}.*")
    # A suite holds many cases: the ctest --timeout (120 s) is per case in spirit, so allow more.
    set_tests_properties(${suite} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 600)
  endforeach()
endif()
