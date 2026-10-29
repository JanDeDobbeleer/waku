# wabar_kernel: helpers with no domain knowledge that two or more units use (proposal, section 6).
#   win32/  Windows build number, WinRT apartment, hidden process launch and shell open
#   qt/     live-desktop gate, polled-source base class, the one app data folder
# Include root is src/ (#include "kernel/qt/app_data.h"). Qt Core and Gui only; the Windows SDK
# only under win32/ (rule S3). Everything links this library through wabar_feature() or by name.
add_library(wabar_kernel STATIC
  qt/app_data.h
  qt/app_data.cpp
  qt/live_desktop.h
  qt/live_desktop.cpp
  qt/polled_source.h
  qt/polled_source.cpp
  win32/shell_launch.h
  win32/shell_launch.cpp
  win32/windows_build.h
  win32/windows_build.cpp
  win32/winrt_apartment.h
  win32/winrt_apartment.cpp
)
target_include_directories(wabar_kernel PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(wabar_kernel
  PUBLIC Qt6::Core Qt6::Gui
  PRIVATE wabar_options shell32 ole32 windowsapp)

wabar_contribute(
  TESTS
    tests/test_app_data.cpp
    tests/test_live_desktop.cpp
    tests/test_polled_source.cpp
    tests/test_windows_build.cpp
  TEST_LIBS wabar_kernel
)
