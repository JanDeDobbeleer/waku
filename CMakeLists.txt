cmake_minimum_required(VERSION 3.25)

project(yasb-native VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "yasb-native builds with MSVC only (see docs/building.md)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# /ZI (edit and continue) is incompatible with /guard:cf
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

# Qt root comes from QT_ROOT (default C:/Qt/6.10.2); YASB_QT_DIR picks the kit folder.
if(DEFINED ENV{QT_ROOT})
  file(TO_CMAKE_PATH "$ENV{QT_ROOT}" YASB_QT_ROOT)
else()
  set(YASB_QT_ROOT "C:/Qt/6.10.2")
endif()
set(YASB_QT_DIR "msvc2022_arm64" CACHE STRING "Qt kit folder under QT_ROOT")
list(PREPEND CMAKE_PREFIX_PATH "${YASB_QT_ROOT}/${YASB_QT_DIR}")
# x64 kit on an ARM64 host needs the native kit for host tools (moc, rcc, ...)
if(YASB_QT_DIR STREQUAL "msvc2022_64" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|arm64"
   AND NOT DEFINED QT_HOST_PATH)
  set(QT_HOST_PATH "${YASB_QT_ROOT}/msvc2022_arm64")
endif()

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

# Applied to our own targets only, so third-party code keeps its warning level.
add_library(yasb_options INTERFACE)
option(YASB_ANALYZE "Enable MSVC static analysis for own targets" OFF)
if(YASB_ANALYZE)
  target_compile_options(yasb_options INTERFACE /analyze /analyze:external-)
endif()
target_compile_features(yasb_options INTERFACE cxx_std_20)
target_compile_definitions(yasb_options INTERFACE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(yasb_options INTERFACE
  /W4 /WX /permissive- /guard:cf /sdl /utf-8 /EHsc /Zc:__cplusplus
  /external:anglebrackets /external:W0)
target_link_options(yasb_options INTERFACE /GUARD:CF /DYNAMICBASE /NXCOMPAT)

# Targets (see docs/architecture.md): wabar -> wabar_* (spike/) -> yasb_widgets -> yasb_ui
#                                                                 -> yasb_platform
# Pipe and watchdog mutex names the host (platform/systray_hook) and the injected DLL (hook/)
# must agree on; distinct from Python YASB's so both builds can coexist.
set(YASB_TRAY_PIPE_NAME "yasb_native_systray_monitor")
set(YASB_TRAY_MUTEX_NAME "YASBNativeTrayHookAlive")

add_subdirectory(hook)
add_subdirectory(platform)
add_subdirectory(core)
add_subdirectory(widgets)
add_subdirectory(spike)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(spike/tests)
endif()
