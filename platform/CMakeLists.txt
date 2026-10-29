# platform: empty. Its Win32 wrappers moved into the features that use them (power, app theme,
# touch keyboard and user info into src/features/control_center). Removed in step 11.
add_library(yasb_platform STATIC
)
target_include_directories(yasb_platform PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_platform
  PUBLIC Qt6::Core
  PRIVATE wabar_options)
