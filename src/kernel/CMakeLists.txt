# wabar_kernel: helpers with no domain knowledge that two or more units use (proposal, section 6).
#   win32/  Windows build number, WinRT apartment, hidden process launch, shell open, app activation,
#           forced foreground focus, shell icons, COM scope, task thread, WinRT strings,
#           environment expansion
#   qt/     live-desktop gate, polled-source base class, the one app data folder, lingering hubs,
#           atomic file write, guarded post to the GUI thread
#   qml/    the QML face of the polled source (compiled into wabar.services, not into this library)
# Include root is src/ (#include "kernel/qt/app_data.h"). Qt Core and Gui only; the Windows SDK
# only under win32/ (rule S3). Everything links this library through wabar_feature() or by name.
add_library(wabar_kernel STATIC
  win32/app_activation.h
  win32/app_activation.cpp
  win32/com_scope.h
  win32/com_scope.cpp
  win32/environment.h
  win32/environment.cpp
  qt/app_data.h
  qt/app_data.cpp
  qt/file_write.h
  qt/file_write.cpp
  qt/gui_post.h
  qt/linger.h
  qt/linger.cpp
  qt/live_desktop.h
  qt/live_desktop.cpp
  qt/polled_source.h
  qt/polled_source.cpp
  win32/shell_icons.h
  win32/shell_icons.cpp
  win32/shell_launch.h
  win32/shell_launch.cpp
  win32/task_thread.h
  win32/task_thread.cpp
  win32/window_focus.h
  win32/window_focus.cpp
  win32/windows_build.h
  win32/windows_build.cpp
  win32/winrt_apartment.h
  win32/winrt_apartment.cpp
  win32/winrt_strings.h
)
target_include_directories(wabar_kernel PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wabar_kernel
  PUBLIC Qt6::Core Qt6::Gui
  PRIVATE wabar_options shell32 ole32 windowsapp propsys user32 version)

# qml/ is the QML face of the polled source. It is not part of wabar_kernel (wabarc links the kernel
# without QtQml); the header is compiled into the wabar.services module like a feature's qml/.
wabar_contribute(
  SERVICES_SOURCES qml/stat_source.h
  SERVICES_INCLUDE_DIRS qml
  TESTS
    tests/test_app_activation.cpp
    tests/test_app_data.cpp
    tests/test_com_scope.cpp
    tests/test_environment.cpp
    tests/test_file_write.cpp
    tests/test_gui_post.cpp
    tests/test_linger.cpp
    tests/test_live_desktop.cpp
    tests/test_polled_source.cpp
    tests/test_window_focus.cpp
    tests/test_shell_icons.cpp
    tests/test_task_thread.cpp
    tests/test_windows_build.cpp
    tests/test_winrt_strings.cpp
  TEST_LIBS wabar_kernel
)
