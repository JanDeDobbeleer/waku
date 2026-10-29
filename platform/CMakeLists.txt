# platform: Win32 wrappers. Depends on nothing of ours; Qt Core only for QMetaType in headers.
add_library(yasb_platform STATIC
  audio.h
  audio.cpp
  bluetooth.h
  bluetooth.cpp
  brightness.h
  brightness.cpp
  command_line.h
  command_line.cpp
  desktop_wallpaper.h
  desktop_wallpaper.cpp
  glazewm.h
  glazewm.cpp
  komorebi.h
  komorebi.cpp
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
  PRIVATE wabar_options wabar_kernel pdh setupapi psapi winmm windowsapp runtimeobject shell32 ole32 propsys version user32
          wlanapi ws2_32 bthprops cfgmgr32 shlwapi dwmapi oleaut32 uuid dxva2 powrprof secur32 advapi32 winhttp)
