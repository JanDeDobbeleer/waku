# platform: Win32 wrappers. Depends on nothing of ours; Qt Core only for QMetaType in headers.
add_library(yasb_platform STATIC
  accent_color.h
  accent_color.cpp
  app_identity.h
  app_identity.cpp
  dwm_thumbnail.h
  dwm_thumbnail.cpp
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
  connectivity.h
  connectivity.cpp
  desktop_wallpaper.h
  desktop_wallpaper.cpp
  github_api.h
  github_api.cpp
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
  pdh.h
  pdh.cpp
  window_focus.h
  window_focus.cpp
  workerw.h
  workerw.cpp
  winrt_apartment.h
  winrt_apartment.cpp
  wlan.h
  wlan.cpp
  dnd.h
  dnd.cpp
  power.h
  power.cpp
  shell_open.h
  shell_open.cpp
  sound.h
  sound.cpp
  theme.h
  theme.cpp
  touch_keyboard.h
  touch_keyboard.cpp
  user_info.h
  user_info.cpp
  system_stats.h
)
target_include_directories(yasb_platform PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_platform
  PUBLIC Qt6::Core
  PRIVATE yasb_options pdh setupapi psapi winmm windowsapp runtimeobject shell32 ole32 propsys version user32
          wlanapi ws2_32 bthprops cfgmgr32 shlwapi dwmapi oleaut32 uuid dxva2 powrprof secur32 advapi32)
# GitHub widget HTTPS (github_api.cpp); headers stay QtCore-only.
find_package(Qt6 REQUIRED COMPONENTS Network)
target_link_libraries(yasb_platform PRIVATE Qt6::Network)
