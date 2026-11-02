# host: the shell runtime (proposal, section 3). Targets:
#
# waku_host_base (STATIC, waku_options): what has no QML in it. screen_match, popup_placement and
#   panel_geometry (pure functions), plugin_guard (checks user-built QML plugins before the engine
#   sees them), startup_trace (WAKU_TRACE=1 timing lines), the app bar interface (app_bar.h) and
#   its Win32 implementation, and the DWM backdrop helpers (win32/). The global hotkey table is
#   kernel::Hotkeys (src/kernel/qt/hotkeys.h).
# waku_host (STATIC + QML module `waku` 2.0): the QML types a shell imports (ShellRoot, Scope,
#   PanelWindow, FloatingWindow, QsWindow, Variants, LazyLoader, ScreenList, Panel, PanelLayout,
#   Theme, PopupWindow, Tooltip, PopupPlacement, WindowThumbnail, the enums) and ShellHost that loads them (qml/),
#   plus the Windows native hooks of ShellHost (win32/).
# The Quickshell compatibility modules that register these types again are src/quickshell/.
#
# testing/ (shell_fixtures.h: the Harness) and tests/ are not compiled into these targets: the
# tests join the one waku_tests binary through waku_contribute(); testing/ is header-only.
# `waku` stays a module of its own: waku.widgets and waku.services import it.
# Include root is src/ (#include "host/qml/shell_host.h"), carried by waku_kernel.
add_library(waku_host_base STATIC
  app_bar.h
  app_bar.cpp
  mask_poller.h
  mask_poller.cpp
  panel_geometry.h
  panel_geometry.cpp
  plugin_guard.h
  plugin_guard.cpp
  popup_placement.h
  popup_placement.cpp
  region_math.h
  region_math.cpp
  screen_match.h
  screen_match.cpp
  startup_trace.h
  startup_trace.cpp
  thumbnail.h
  thumbnail.cpp
  thumbnail_store.h
  thumbnail_store.cpp
  windows_taskbar.h
  windows_taskbar.cpp
  win32/app_bar.cpp
  win32/backdrop.h
  win32/backdrop.cpp
  win32/windows_taskbar_win.cpp
  win32/thumbnail_win.cpp
)
target_link_libraries(waku_host_base
  PUBLIC waku_kernel
  PRIVATE waku_options dwmapi shell32 user32)

set(host_qml_files
  qml/FallbackShell.qml
  qml/Panel.qml
  qml/PanelLayout.qml
  qml/PopupWindow.qml
  qml/PopupWindowBase.qml
  qml/Tooltip.qml
  qml/Variants.qml
  qml/WindowChrome.qml)
# The files live in qml/ but keep their resource paths (qrc:/qt/qml/waku/WindowChrome.qml).
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
qt_add_library(waku_host STATIC)
qt_add_qml_module(waku_host
  URI waku
  VERSION 2.0
  NO_GENERATE_EXTRA_QMLDIRS
  QML_FILES ${host_qml_files}
  SOURCES
    qml/control_handler.h
    qml/control_handler.cpp
    qml/floating_window.h
    qml/floating_window.cpp
    qml/global_shortcut.h
    qml/global_shortcut.cpp
    qml/ipc_handler.h
    qml/ipc_handler.cpp
    qml/lazy_loader.h
    qml/lazy_loader.cpp
    qml/panel_window.h
    qml/panel_window.cpp
    qml/persistent_properties.h
    qml/persistent_properties.cpp
    qml/popup_placement_qml.h
    qml/qs_window.h
    qml/qs_window.cpp
    qml/region.h
    qml/region.cpp
    qml/scope.h
    qml/script_model.h
    qml/script_model.cpp
    qml/shell_host.h
    qml/shell_host.cpp
    qml/shell_screen.h
    qml/shell_screen.cpp
    qml/surface_control.h
    qml/surface_control.cpp
    qml/system_clock.h
    qml/system_clock.cpp
    qml/theme.h
    qml/theme.cpp
    qml/thumbnails.h
    qml/thumbnails.cpp
    qml/transform_watcher.h
    qml/transform_watcher.cpp
    qml/window_enums.h
    qml/window_thumbnail.h
    qml/window_thumbnail.cpp
    qml/windows_taskbar_command.h
    qml/windows_taskbar_command.cpp
    qml/wlr_layershell.h
    qml/wlr_layershell.cpp
    win32/shell_host_win32.cpp
)
# The generated type registration includes headers by name, hence the private include directory.
target_include_directories(waku_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
# QmlPrivate: ShellHost reads a compiled shell's root type without creating it (shell_host.cpp).
target_link_libraries(waku_host
  PUBLIC Qt6::Quick Qt6::Qml waku_host_base waku_control waku_kernel
  PRIVATE waku_options Qt6::QmlPrivate)
waku_qt_generated_code(waku_host)

waku_contribute(
  TESTS
    tests/test_app_bar.cpp
    tests/test_control_handler.cpp
    tests/test_global_shortcut.cpp
    tests/test_image_formats.cpp
    tests/test_overlays.cpp
    tests/test_panel_geometry.cpp
    tests/test_mask_poller.cpp
    tests/test_popups.cpp
    tests/test_region_math.cpp
    tests/test_region_mask.cpp
    tests/test_screen_match.cpp
    tests/test_shell_host.cpp
    tests/test_shell_screen.cpp
    tests/test_shell_windows.cpp
    tests/test_windows_taskbar.cpp
    tests/test_theme.cpp
    tests/test_thumbnails.cpp
    tests/test_window_thumbnail.cpp
  TEST_LIBS waku_host_base
)
