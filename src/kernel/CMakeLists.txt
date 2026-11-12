# waku_kernel: helpers with no domain knowledge that two or more units use (proposal, section 6).
#   win32/  Windows build number, WinRT apartment, hidden process launch, shell open, app activation,
#           forced foreground focus, the RAII WinEvent hook, shell icons, COM scope, task thread, WinRT strings,
#           environment expansion, the RegisterHotKey backend of the hotkey table
#   qt/     live-desktop gate, polled-source base class, the one app data folder, lingering hubs,
#           atomic file write, guarded post to the GUI thread, the global hotkey table (Hotkeys)
#   qml/    the QML face of the polled source (compiled into waku.services, not into this library)
#   testing/ header-only fakes for tests (the fake hotkey backend)
# Include root is src/ (#include "kernel/qt/app_data.h"). Qt Core and Gui only; the Windows SDK
# only under win32/ (rule S3). Everything links this library through waku_feature() or by name.
add_library(waku_kernel STATIC
  win32/app_activation.h
  win32/app_activation.cpp
  win32/com_scope.h
  win32/com_scope.cpp
  win32/environment.h
  win32/environment.cpp
  win32/hotkeys_win.cpp
  qt/app_data.h
  qt/app_data.cpp
  qt/file_write.h
  qt/file_write.cpp
  qt/gui_post.h
  qt/hotkeys.h
  qt/hotkeys.cpp
  qt/linger.h
  qt/linger.cpp
  qt/live_desktop.h
  qt/live_desktop.cpp
  qt/polled_source.h
  qt/sealed_singleton.h
  qt/polled_source.cpp
  win32/shell_icons.h
  win32/shell_icons.cpp
  win32/shell_launch.h
  win32/shell_launch.cpp
  win32/task_thread.h
  win32/task_thread.cpp
  win32/window_focus.h
  win32/window_focus.cpp
  win32/winevent_hook.h
  win32/winevent_hook.cpp
  win32/windows_build.h
  win32/windows_build.cpp
  win32/winrt_apartment.h
  win32/winrt_apartment.cpp
  win32/winrt_strings.h
)
target_include_directories(waku_kernel PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(waku_kernel
  PUBLIC Qt6::Core Qt6::Gui
  PRIVATE waku_options shell32 ole32 windowsapp propsys user32 version)
# The metatypes of PolledSource reach waku.services' type registrar this way. Without them the
# registrar cannot see the QObject base of StatSource, records QQmlParserStatus as its prototype,
# and qmllint cannot resolve Cpu, Memory, Gpu or Disks.
qt_extract_metatypes(waku_kernel)

# qml/ is the QML face of the polled source. It is not part of waku_kernel (wakuc links the kernel
# without QtQml); the header is compiled into the waku.services module like a feature's qml/.
waku_contribute(
  SERVICES_SOURCES qml/stat_source.h
  SERVICES_INCLUDE_DIRS qml
  TESTS
    tests/test_app_activation.cpp
    tests/test_app_data.cpp
    tests/test_com_scope.cpp
    tests/test_environment.cpp
    tests/test_file_write.cpp
    tests/test_gui_post.cpp
    tests/test_hotkeys.cpp
    tests/test_linger.cpp
    tests/test_live_desktop.cpp
    tests/test_polled_source.cpp
    tests/test_window_focus.cpp
    tests/test_shell_icons.cpp
    tests/test_task_thread.cpp
    tests/test_windows_build.cpp
    tests/test_winrt_strings.cpp
  TEST_LIBS waku_kernel
)
