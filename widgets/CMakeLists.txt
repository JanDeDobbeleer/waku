# widgets: the service and state code behind the wabar widgets (GitHub, GlazeWM, komorebi, systray,
# taskbar, wallpapers). No UI of its own: the QML widgets live in spike/widgets.
add_library(yasb_widgets STATIC
  github/github_format.cpp
  github/github_format.h
  github/github_icons.h
  glazewm/glazewm_service.cpp
  glazewm/glazewm_service.h
  glazewm/glazewm_state.cpp
  glazewm/glazewm_state.h
  komorebi/komorebi_service.cpp
  komorebi/komorebi_service.h
  komorebi/komorebi_state.cpp
  komorebi/komorebi_state.h
)
target_include_directories(yasb_widgets PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_widgets
  PUBLIC yasb_ui yasb_platform nlohmann_json::nlohmann_json
  PRIVATE wabar_options wabar_kernel)
