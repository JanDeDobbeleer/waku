# barnacle_control: the named-pipe channel between a running bar and `barnaclec`. Qt Core and
# Network only, so the command-line client stays small.
qt_add_library(barnacle_control STATIC
  control.h
  control.cpp
)
target_include_directories(barnacle_control PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(barnacle_control PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(barnacle_control PUBLIC Qt6::Core Qt6::Network)
