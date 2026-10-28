# YASBNativeTrayHook[_arm64].dll: the DLL the native host injects into Explorer so it can see
# Shell_TrayWnd's WM_COPYDATA tray traffic. Source is a copy of Python YASB's trayhook.cpp with the
# pipe and watchdog mutex names taken from the compile definitions below (set in the root
# CMakeLists.txt, shared with platform/systray_hook).
#
# The DLL must match Explorer's architecture, so each preset builds the DLL for its own target
# (arm64-* -> YASBNativeTrayHook_arm64.dll, x64-release -> YASBNativeTrayHook.dll). An x64 build
# running under emulation on an ARM64 host cannot inject it and falls back to the legacy window.
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
  set(_hook_suffix "_arm64")
  set(_hook_arm64 TRUE)
else()
  set(_hook_suffix "")
  set(_hook_arm64 FALSE)
endif()

add_library(YASBNativeTrayHook SHARED trayhook.cpp version.rc)
set_target_properties(YASBNativeTrayHook PROPERTIES
  OUTPUT_NAME "YASBNativeTrayHook${_hook_suffix}"
  AUTOMOC OFF
  # Static CRT: the DLL lives inside Explorer, which does not ship ours.
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  # Next to yasb-native.exe (app/ sets the same folder).
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_compile_definitions(YASBNativeTrayHook PRIVATE
  YASB_TRAY_PIPE_NAME="${YASB_TRAY_PIPE_NAME}" YASB_TRAY_MUTEX_NAME="${YASB_TRAY_MUTEX_NAME}")
target_compile_options(YASBNativeTrayHook PRIVATE /guard:cf /GS /sdl)
if(_hook_arm64)
  target_compile_definitions(YASBNativeTrayHook PRIVATE BUILD_ARM64)
  target_link_options(YASBNativeTrayHook PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /CETCOMPAT:NO)
else()
  target_link_options(YASBNativeTrayHook PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /CETCOMPAT)
endif()
