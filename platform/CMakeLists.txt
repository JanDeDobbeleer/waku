# platform: Win32 wrappers. Depends on nothing of ours; Qt Core only for QMetaType in headers.
add_library(yasb_platform STATIC
  app_identity.h
  app_identity.cpp
  pin_support.h
  pin_support.cpp
  window_system.h
  window_system_win32.cpp
  window_tracker.h
  window_tracker.cpp
  window_tracker_hooks_win32.cpp
  audio.h
  audio.cpp
  battery.h
  battery.cpp
  bluetooth.h
  bluetooth.cpp
  brightness.h
  brightness.cpp
  command_line.h
  command_line.cpp
  connectivity.h
  connectivity.cpp
  desktop_wallpaper.h
  desktop_wallpaper.cpp
  github_api.h
  github_api.cpp
  glazewm.h
  glazewm.cpp
  komorebi.h
  komorebi.cpp
  media_session.h
  media_session.cpp
  media_session_winrt.cpp
  media_source_apps.cpp
  memory.h
  memory.cpp
  notifications.h
  notifications.cpp
  notifications_system.cpp
  toast_activation.h
  toast_activation.cpp
  toast_xml.h
  toast_xml.cpp
  pdh.h
  pdh.cpp
  window_focus.h
  window_focus.cpp
  workerw.h
  workerw.cpp
  wlan.h
  wlan.cpp
  dnd.h
  dnd.cpp
  power.h
  power.cpp
  theme.h
  theme.cpp
  touch_keyboard.h
  touch_keyboard.cpp
  user_info.h
  user_info.cpp
  system_stats.h
  systray_click.h
  systray_click.cpp
  systray_hook.h
  systray_hook.cpp
  systray_host.h
  systray_pipe.h
  systray_pipe.cpp
  systray_types.h
  systray_win.h
  systray_win.cpp
)
target_include_directories(yasb_platform PUBLIC ${CMAKE_SOURCE_DIR})
# Pipe and watchdog mutex names shared with hook/trayhook.cpp (set in the root CMakeLists).
target_compile_definitions(yasb_platform PUBLIC
  YASB_TRAY_PIPE_NAME="${YASB_TRAY_PIPE_NAME}" YASB_TRAY_MUTEX_NAME="${YASB_TRAY_MUTEX_NAME}")
target_link_libraries(yasb_platform
  PUBLIC Qt6::Core
  PRIVATE wabar_options wabar_kernel pdh setupapi psapi winmm windowsapp runtimeobject shell32 ole32 propsys version user32
          wlanapi ws2_32 bthprops cfgmgr32 shlwapi dwmapi oleaut32 uuid dxva2 powrprof secur32 advapi32 winhttp)
# GitHub widget HTTPS (github_api.cpp); headers stay QtCore-only.
find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(yasb_platform PRIVATE Qt6::Network)
