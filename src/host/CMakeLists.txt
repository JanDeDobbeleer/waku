# host: the bar runtime (proposal, section 3). Two targets:
#
# wabar_host_base (STATIC, wabar_options): what has no QML in it. screen_match and popup_placement
#   (pure functions), the app bar interface (app_bar.h) and its Win32 implementation, and the DWM
#   backdrop helpers (win32/).
# wabar_host (STATIC + QML module `wabar`, URI and version unchanged): the QML types a config
#   imports (Config, Bar, BarLayout, Theme, PopupWindow, Tooltip, PopupPlacement) and BarHost that
#   builds the windows from them (qml/), plus the Windows native hooks of BarHost (win32/).
#   Like the other QML module targets it does not link wabar_options until step 9.
#
# testing/ (bar_fixtures.h: the Harness) and tests/ are not compiled into either target: the
# tests join the one wabar_tests binary through wabar_contribute(); testing/ is header-only.
# `wabar` stays a module of its own: wabar.widgets and wabar.services import it.
# Include root is src/ (#include "host/qml/bar_host.h"), carried by wabar_kernel.
add_library(wabar_host_base STATIC
  app_bar.h
  popup_placement.h
  popup_placement.cpp
  screen_match.h
  screen_match.cpp
  win32/app_bar.cpp
  win32/backdrop.h
  win32/backdrop.cpp
)
target_link_libraries(wabar_host_base
  PUBLIC wabar_kernel
  PRIVATE wabar_options dwmapi shell32 user32)

set(host_qml_files
  qml/BarLayout.qml
  qml/BarWindow.qml
  qml/FallbackConfig.qml
  qml/PopupWindow.qml
  qml/Tooltip.qml)
# The files live in qml/ but keep their resource paths (qrc:/qt/qml/wabar/BarWindow.qml).
foreach(file IN LISTS host_qml_files)
  cmake_path(GET file FILENAME name)
  set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()

qt_add_library(wabar_host STATIC)
qt_add_qml_module(wabar_host
  URI wabar
  VERSION 1.0
  NO_GENERATE_EXTRA_QMLDIRS
  QML_FILES ${host_qml_files}
  SOURCES
    qml/bar_config.h
    qml/bar_host.h
    qml/bar_host.cpp
    qml/bar_theme.h
    qml/bar_theme.cpp
    qml/control_handler.h
    qml/control_handler.cpp
    qml/popup_placement_qml.h
    win32/bar_host_win32.cpp
)
# The generated type registration includes headers by name, hence the private include directory.
target_include_directories(wabar_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
target_compile_definitions(wabar_host PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(wabar_host PUBLIC Qt6::Quick Qt6::Qml wabar_host_base wabar_control wabar_kernel)

wabar_contribute(
  TESTS
    tests/test_bar_host.cpp
    tests/test_control_handler.cpp
    tests/test_popups.cpp
    tests/test_screen_match.cpp
    tests/test_theme.cpp
  TEST_LIBS wabar_host_base
)
