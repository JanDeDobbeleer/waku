# platform: Win32 wrappers. Depends on nothing of ours; Qt Core only for QMetaType in headers.
add_library(yasb_platform STATIC
  battery.h
  battery.cpp
  bluetooth.h
  bluetooth.cpp
  connectivity.h
  connectivity.cpp
  memory.h
  memory.cpp
  pdh.h
  pdh.cpp
  winrt_apartment.h
  winrt_apartment.cpp
  wlan.h
  wlan.cpp
  system_stats.h
)
target_include_directories(yasb_platform PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_platform
  PUBLIC Qt6::Core
  PRIVATE yasb_options pdh setupapi psapi wlanapi ws2_32 bthprops cfgmgr32 ole32 windowsapp runtimeobject)
