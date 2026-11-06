# wakukeys.exe: the hotkey daemon (waku_keymap) and its control commands (reload, stop, status). A
# Windows (GUI) program with no console, started once per user. Links the control channel and the
# keymap feature only, no Qt Quick. waku.exe and wakuc.exe do not link waku_keymap.
qt_add_executable(wakukeys WIN32 main.cpp keys_args.cpp keys_args.h keys_daemon.cpp keys_daemon.h)
target_link_libraries(wakukeys PRIVATE waku_options waku_control waku_keymap user32)
set_target_properties(wakukeys PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# The argument parser is the one part of the program a test links (tests/CMakeLists.txt).
