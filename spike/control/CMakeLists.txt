# wabar_control: the named-pipe channel between a running bar and `wabarc`. Qt Core and
# Network only, so the command-line client stays small.
qt_add_library(wabar_control STATIC
  control.h
  control.cpp
  wallpaper_control.h
)
target_include_directories(wabar_control PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(wabar_control PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(wabar_control PUBLIC Qt6::Core Qt6::Network)
