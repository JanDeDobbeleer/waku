# waku_control: the named-pipe channel between a running bar and `wakuc` (proposal, section 3):
# the line protocol, the server, the blocking client and the quoting of a command line. It knows
# no command: the bar's handler (host) and the verbs waku.exe adds answer them. Qt Core and
# Network only (no Qt Quick), so the command-line client stays small; win32/ holds the
# single-instance mutex. Include root is src/ (#include "control/control.h").
add_library(waku_control STATIC
  control.h
  control.cpp
  config_init.h
  config_init.cpp
  win32/instance_mutex.h
  win32/instance_mutex.cpp
)
target_link_libraries(waku_control
  PUBLIC waku_kernel Qt6::Core Qt6::Network
  PRIVATE waku_options)

waku_contribute(
  TESTS
    tests/test_config_init.cpp
    tests/test_control.cpp
    tests/test_wallpaper_quoting.cpp
  TEST_LIBS waku_control
)
