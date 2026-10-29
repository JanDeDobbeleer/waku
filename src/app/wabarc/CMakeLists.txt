# wabarc.exe: the console client (reload, status, quit, wallpaper). Links the control channel only,
# no Qt Quick.
add_executable(wabarc main.cpp)
target_compile_definitions(wabarc PRIVATE
  WABAR_SAMPLE_CONFIG_DIR="${PROJECT_SOURCE_DIR}/examples/config")
target_link_libraries(wabarc PRIVATE wabar_options wabar_control)
set_target_properties(wabarc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
