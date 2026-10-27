include(GoogleTest)

add_executable(yasb_tests smoke/test_startup.cpp)
target_link_libraries(yasb_tests PRIVATE yasb_core yasb_options GTest::gtest_main)

# Qt DLLs and plugins must sit next to the test exe so ctest discovery and runs work without PATH.
add_custom_command(TARGET yasb_tests POST_BUILD
  COMMAND Qt6::windeployqt $<IF:$<CONFIG:Debug>,--debug,--release>
          --no-translations --no-system-d3d-compiler --no-opengl-sw
                    --include-plugins qoffscreen          --dir "$<TARGET_FILE_DIR:yasb_tests>" "$<TARGET_FILE:yasb_tests>"
  VERBATIM)

# Discovery runs at test time so cross-built binaries don't need to execute during the build.
gtest_discover_tests(yasb_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- U4: CSS preprocessor / rules / transition engine ---
find_package(Qt6 REQUIRED COMPONENTS Test)
target_sources(yasb_tests PRIVATE css/test_preprocessor.cpp css/test_transitions.cpp)
target_compile_definitions(yasb_tests PRIVATE YASB_VECTORS_DIR="${CMAKE_SOURCE_DIR}/tests/vectors")
target_link_libraries(yasb_tests PRIVATE nlohmann_json::nlohmann_json Qt6::Test)
