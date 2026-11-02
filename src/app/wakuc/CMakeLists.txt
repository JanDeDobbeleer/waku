# wakuc.exe: the console client (reload, status, quit, wallpaper, surface). Links the control channel only,
# no Qt Quick.
add_executable(wakuc main.cpp)
target_link_libraries(wakuc PRIVATE waku_options waku_control user32)
set_target_properties(wakuc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
