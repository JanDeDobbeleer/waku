# keymap: the hotkey daemon of `wakuc keys`. Loads a whkd binding file, registers it in the kernel
# hotkey table and runs the actions. Linked into wakuc only (src/app/wakuc), never into waku.exe.
waku_feature(NAME keymap
  SOURCES
    config_watcher.cpp
    config_watcher.h
    key_names.cpp
    key_names.h
    keymap_action.cpp
    keymap_action.h
    keymap_daemon.cpp
    keymap_daemon.h
    keymap_parser.cpp
    keymap_parser.h
    keymap_system.h
    keymap_types.h
    window_match.cpp
    window_match.h
  WIN32_SOURCES
    win32/keymap_system_win.cpp
  TESTS
    tests/test_keymap_action.cpp
    tests/test_keymap_daemon.cpp
    tests/test_keymap_parser.cpp
    tests/test_keymap_window_match.cpp
  SYSTEM_LIBS user32)
# Fakes (testing/keymap_fakes.h) and the sample file (testing/whkdrc_sample.h) are header-only and
# included by the tests only.
target_link_libraries(waku_keymap PUBLIC waku_control)
