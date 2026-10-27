include(GoogleTest)

add_executable(yasb_tests smoke/test_startup.cpp)
# One test binary covers every layer, so it links the top library (yasb_app).
target_link_libraries(yasb_tests PRIVATE yasb_app yasb_options GTest::gtest_main)

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

# --- config loading (U3) ---
target_sources(yasb_tests PRIVATE
  config/test_loader.cpp
  config/test_parity_config.cpp)
target_compile_definitions(yasb_tests PRIVATE YASB_VECTORS_DIR="${CMAKE_SOURCE_DIR}/tests/vectors")

# --- U4: CSS preprocessor / rules / transition engine ---
find_package(Qt6 REQUIRED COMPONENTS Test)
target_sources(yasb_tests PRIVATE css/test_preprocessor.cpp css/test_transitions.cpp)
target_link_libraries(yasb_tests PRIVATE nlohmann_json::nlohmann_json Qt6::Test)

# --- U5: bar shell and adaptive frame ---
target_sources(yasb_tests PRIVATE bar/test_adaptive_frame.cpp bar/test_bar_window.cpp)

# --- U6: widget framework ---
# --- U6: widget framework ---
target_sources(yasb_tests PRIVATE
  widgets/test_label_format.cpp
  widgets/test_callbacks.cpp
  widgets/test_popup.cpp
  widgets/test_stat_popup.cpp
  widgets/test_system_widgets.cpp)

# --- U7: app services and CLI protocol ---
target_sources(yasb_tests PRIVATE
  app/test_cli_protocol.cpp
  app/test_file_watcher.cpp
  app/test_hotkeys.cpp
  app/test_single_instance.cpp
)
