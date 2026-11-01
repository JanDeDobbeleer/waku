cmake_minimum_required(VERSION 3.25)

project(wabar VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "wabar builds with MSVC only (see docs/building.md)")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
# /ZI (edit and continue) is incompatible with /guard:cf
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

# Qt root comes from QT_ROOT (default C:/Qt/6.10.2); WABAR_QT_DIR picks the kit folder.
if(DEFINED ENV{QT_ROOT})
  file(TO_CMAKE_PATH "$ENV{QT_ROOT}" WABAR_QT_ROOT)
else()
  set(WABAR_QT_ROOT "C:/Qt/6.10.2")
endif()
set(WABAR_QT_DIR "msvc2022_arm64" CACHE STRING "Qt kit folder under QT_ROOT")
list(PREPEND CMAKE_PREFIX_PATH "${WABAR_QT_ROOT}/${WABAR_QT_DIR}")
# x64 kit on an ARM64 host needs the native kit for host tools (moc, rcc, ...)
if(WABAR_QT_DIR STREQUAL "msvc2022_64" AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "ARM64|arm64"
   AND NOT DEFINED QT_HOST_PATH)
  set(QT_HOST_PATH "${WABAR_QT_ROOT}/msvc2022_arm64")
endif()

find_package(Qt6 6.10 REQUIRED COMPONENTS Core Gui Qml Quick Network)
qt_policy(SET QTP0001 NEW)  # QML module resources under :/qt/qml/<URI>
find_package(nlohmann_json CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(wabar_options)   # wabar_options: flags for our own targets
include(wabar_feature)   # wabar_feature(), wabar_contribute()

# Targets: see docs/architecture.md. src/CMakeLists.txt adds every unit; the QML modules are
# assembled from their contributions (src/modules/qml_modules.cmake).
# Pipe and watchdog mutex names the host (features/tray/win32/tray_hook) and the injected DLL (features/tray/hook/)
# must agree on; distinct from Python YASB's so both builds can coexist.
set(WABAR_TRAY_PIPE_NAME "wabar_systray_monitor")
set(WABAR_TRAY_MUTEX_NAME "WabarTrayHookAlive")

add_subdirectory(src)
include(wabar_deploy)    # the deploy target (needs the targets of src/)
include(wabar_examples_lint)  # qmllint over examples/, part of all_qmllint

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()
