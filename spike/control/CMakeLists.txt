# tidy_control: the named-pipe channel between a running bar and `tidyc`. Qt Core and
# Network only, so the command-line client stays small.
qt_add_library(tidy_control STATIC
  control.h
  control.cpp
)
target_include_directories(tidy_control PUBLIC ${CMAKE_SOURCE_DIR})
target_compile_definitions(tidy_control PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(tidy_control PUBLIC Qt6::Core Qt6::Network)
