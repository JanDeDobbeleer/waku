# wabar_control: the named-pipe channel between a running bar and `wabarc` (proposal, section 3):
# the line protocol, the server, the blocking client and the quoting of a command line. It knows
# no command: the bar's handler (host) and the verbs wabar.exe adds answer them. Qt Core and
# Network only (no Qt Quick), so the command-line client stays small; win32/ holds the
# single-instance mutex. Include root is src/ (#include "control/control.h").
add_library(wabar_control STATIC
  control.h
  control.cpp
  win32/instance_mutex.h
  win32/instance_mutex.cpp
)
target_link_libraries(wabar_control
  PUBLIC wabar_kernel Qt6::Core Qt6::Network
  PRIVATE wabar_options)

wabar_contribute(
  TESTS
    tests/test_control.cpp
    tests/test_wallpaper_quoting.cpp
  TEST_LIBS wabar_control
)
