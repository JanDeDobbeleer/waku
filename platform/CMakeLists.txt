# platform: Win32 wrappers. Depends on nothing of ours; Qt Core only for QMetaType in headers.
add_library(yasb_platform STATIC
  battery.h
  battery.cpp
  github_api.h
  github_api.cpp
  komorebi.h
  komorebi.cpp
  memory.h
  memory.cpp
  pdh.h
  pdh.cpp
  sound.h
  sound.cpp
  system_stats.h
)
target_include_directories(yasb_platform PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_platform
  PUBLIC Qt6::Core
  PRIVATE yasb_options pdh setupapi psapi winmm)
