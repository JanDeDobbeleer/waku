# host: the shell runtime (proposal, section 3). Targets:
#
# wabar_host_base (STATIC, wabar_options): what has no QML in it. screen_match, popup_placement and
#   panel_geometry (pure functions), the app bar interface (app_bar.h) and its Win32
#   implementation, and the DWM backdrop helpers (win32/).
# wabar_host (STATIC + QML module `wabar` 2.0): the QML types a shell imports (ShellRoot, Scope,
#   PanelWindow, FloatingWindow, QsWindow, Variants, LazyLoader, ScreenList, Panel, PanelLayout,
#   Theme, PopupWindow, Tooltip, PopupPlacement, the enums) and ShellHost that loads them (qml/),
#   plus the Windows native hooks of ShellHost (win32/).
# quickshell/: the modules `Quickshell` and `Quickshell.Wayland`, the same types under Quickshell's
#   names so ported shells load (QML_FOREIGN registrations, no code of their own).
#
# testing/ (shell_fixtures.h: the Harness) and tests/ are not compiled into these targets: the
# tests join the one wabar_tests binary through wabar_contribute(); testing/ is header-only.
# `wabar` stays a module of its own: wabar.widgets and wabar.services import it.
# Include root is src/ (#include "host/qml/shell_host.h"), carried by wabar_kernel.
add_library(wabar_host_base STATIC
  app_bar.h
  app_bar.cpp
  panel_geometry.h
  panel_geometry.cpp
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
  qml/FallbackShell.qml
  qml/Panel.qml
  qml/PanelLayout.qml
  qml/PopupWindow.qml
  qml/Tooltip.qml
  qml/Variants.qml
  qml/WindowChrome.qml)
# The files live in qml/ but keep their resource paths (qrc:/qt/qml/wabar/WindowChrome.qml).
foreach(file IN LISTS host_qml_files)
  cmake_path(GET file FILENAME name)
  set_source_files_properties(${file} PROPERTIES QT_RESOURCE_ALIAS ${name})
endforeach()
# The host's own windows and recovery shell are not for user QML.
set_source_files_properties(qml/WindowChrome.qml qml/FallbackShell.qml
  PROPERTIES QT_QML_INTERNAL_TYPE TRUE)

# Qt 6.9+ ships the private targets as packages of their own; the one use is deliberate.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 REQUIRED COMPONENTS QmlPrivate)
qt_add_library(wabar_host STATIC)
qt_add_qml_module(wabar_host
  URI wabar
  VERSION 2.0
  NO_GENERATE_EXTRA_QMLDIRS
  QML_FILES ${host_qml_files}
  SOURCES
    qml/bar_theme.h
    qml/bar_theme.cpp
    qml/control_handler.h
    qml/control_handler.cpp
    qml/lazy_loader.h
    qml/lazy_loader.cpp
    qml/popup_placement_qml.h
    qml/shell_config.h
    qml/shell_host.h
    qml/shell_host.cpp
    qml/shell_screen.h
    qml/shell_screen.cpp
    qml/shell_window.h
    qml/shell_window.cpp
    qml/window_enums.h
    win32/shell_host_win32.cpp
)
# The generated type registration includes headers by name, hence the private include directory.
target_include_directories(wabar_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
# QmlPrivate: ShellHost reads a compiled shell's root type without creating it (shell_host.cpp).
target_link_libraries(wabar_host
  PUBLIC Qt6::Quick Qt6::Qml wabar_host_base wabar_control wabar_kernel
  PRIVATE wabar_options Qt6::QmlPrivate)
wabar_qt_generated_code(wabar_host)

add_subdirectory(quickshell)

wabar_contribute(
  TESTS
    tests/test_app_bar.cpp
    tests/test_control_handler.cpp
    tests/test_panel_geometry.cpp
    tests/test_popups.cpp
    tests/test_quickshell.cpp
    tests/test_screen_match.cpp
    tests/test_shell_host.cpp
    tests/test_shell_windows.cpp
    tests/test_theme.cpp
    tests/test_wallpaper_handler.cpp
  TEST_LIBS wabar_host_base
)
