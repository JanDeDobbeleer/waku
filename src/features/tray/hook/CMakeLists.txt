# WabarTrayHook[_arm64].dll: the DLL the native host injects into Explorer so it can see
# Shell_TrayWnd's WM_COPYDATA tray traffic. Source is a copy of Python YASB's trayhook.cpp with the
# pipe and watchdog mutex names taken from the compile definitions below (set in the root
# CMakeLists.txt, shared with win32/tray_hook).
#
# The DLL must match Explorer's architecture, so each preset builds the DLL for its own target
# (arm64-* -> WabarTrayHook_arm64.dll, x64-release -> WabarTrayHook.dll). An x64 build
# running under emulation on an ARM64 host cannot inject it, so it has no tray host.
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
  set(_hook_suffix "_arm64")
  set(_hook_arm64 TRUE)
else()
  set(_hook_suffix "")
  set(_hook_arm64 FALSE)
endif()

add_library(WabarTrayHook SHARED trayhook.cpp version.rc)
set_target_properties(WabarTrayHook PROPERTIES
  OUTPUT_NAME "WabarTrayHook${_hook_suffix}"
  AUTOMOC OFF
  # Static CRT: the DLL lives inside Explorer, which does not ship ours.
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  # Next to wabar.exe (src/app/wabar sets the same folder).
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
target_compile_definitions(WabarTrayHook PRIVATE
  WABAR_TRAY_PIPE_NAME="${WABAR_TRAY_PIPE_NAME}" WABAR_TRAY_MUTEX_NAME="${WABAR_TRAY_MUTEX_NAME}")
# Strict warnings like wabar_options, set by hand: the DLL has no Qt, a static CRT and its own
# definitions, so it does not link wabar_options.
target_compile_options(WabarTrayHook PRIVATE /W4 /WX /guard:cf /GS /sdl)
if(_hook_arm64)
  target_compile_definitions(WabarTrayHook PRIVATE BUILD_ARM64)
  target_link_options(WabarTrayHook PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /CETCOMPAT:NO)
else()
  target_link_options(WabarTrayHook PRIVATE /guard:cf /DYNAMICBASE /NXCOMPAT /CETCOMPAT)
endif()
