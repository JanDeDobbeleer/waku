# Unit tests of the Yasb.Services types (spike/services). Widgets (QML) are out of scope.
include(GoogleTest)
find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(yasb_services_tests
  test_main.cpp
  test_stat_sources.cpp
  test_process.cpp
  test_audio.cpp
  test_media.cpp
  test_wm_clients.cpp
)
target_compile_definitions(yasb_services_tests PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(yasb_services_tests PRIVATE
  yasb_services GTest::gtest Qt6::Test nlohmann_json::nlohmann_json)
set_target_properties(yasb_services_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Qt DLLs sit next to the test exe so ctest runs without PATH. No platform plugin: the tests use
# QCoreApplication.
add_custom_command(TARGET yasb_services_tests POST_BUILD
  COMMAND Qt6::windeployqt $<IF:$<CONFIG:Debug>,--debug,--release>
          --no-translations --no-system-d3d-compiler --no-opengl-sw --no-compiler-runtime
          --dir "$<TARGET_FILE_DIR:yasb_services_tests>" "$<TARGET_FILE:yasb_services_tests>"
  VERBATIM)

gtest_discover_tests(yasb_services_tests DISCOVERY_MODE PRE_TEST)
