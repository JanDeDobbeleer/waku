# wabar_tests: the one test binary (proposal, section 8). Its sources are main.cpp here plus every
# TESTS contribution: wabar_feature(... TESTS ...) under src/ and wabar_contribute(TESTS ...) in the
# legacy spike/tests/. Added after every contributor (top-level CMakeLists.txt).
# Select one feature by suite name: ctest -R Tray, or --gtest_filter=Tray*.
include(GoogleTest)
find_package(Qt6 REQUIRED COMPONENTS Test)

get_property(test_sources GLOBAL PROPERTY WABAR_TEST_SOURCES)
get_property(test_libs GLOBAL PROPERTY WABAR_TEST_LIBS)
list(REMOVE_DUPLICATES test_libs)

add_executable(wabar_tests main.cpp ${test_sources})
# The control tests run wabarc.exe.
add_dependencies(wabar_tests wabarc)
target_compile_definitions(wabar_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# The static QML plugins are linked so `import wabar` resolves in the test engines.
target_link_libraries(wabar_tests PRIVATE
  wabar_host wabar_hostplugin wabar_widgets wabar_widgetsplugin
  wabar_services wabar_servicesplugin
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
          --qmldir "${PROJECT_SOURCE_DIR}/spike/widgets"
          --qmldir "${PROJECT_SOURCE_DIR}/src"
          --dir "$<TARGET_FILE_DIR:wabar_tests>" "$<TARGET_FILE:wabar_tests>"
  VERBATIM)

gtest_discover_tests(wabar_tests DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
