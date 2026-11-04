# wakuc.exe: the console client (reload, status, quit, wallpaper, surface, config init) and the
# hotkey daemon `wakuc keys` (waku_keymap). Links the control channel only, no Qt Quick. waku.exe
# does not link waku_keymap.
add_executable(wakuc main.cpp keys_command.cpp keys_command.h)
target_link_libraries(wakuc PRIVATE waku_options waku_control waku_startup waku_keymap user32)
set_target_properties(wakuc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# `wakuc config init` writes the built-in bar (waku's DefaultShell.qml) as the starter shell. The
# one source file is embedded, not copied: it is read at run time as ":/waku/DefaultShell.qml".
qt_add_resources(wakuc wakuc_template
  PREFIX "/waku"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/../waku"
  FILES ../waku/DefaultShell.qml)
