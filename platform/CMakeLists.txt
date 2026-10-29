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
  battery.h
  battery.cpp
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
)
target_include_directories(yasb_platform PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_platform
  PUBLIC Qt6::Core
  PRIVATE wabar_options wabar_kernel psapi winmm windowsapp runtimeobject shell32 ole32 propsys version user32
          wlanapi ws2_32 bthprops cfgmgr32 shlwapi dwmapi oleaut32 uuid dxva2 powrprof secur32 advapi32)
