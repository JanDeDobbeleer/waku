# widgets: the service and state code behind the wabar widgets (GlazeWM, komorebi, systray,
# taskbar, wallpapers). No UI of its own: the QML widgets live in spike/widgets.
add_library(yasb_widgets STATIC
  glazewm/glazewm_service.cpp
  glazewm/glazewm_service.h
  glazewm/glazewm_state.cpp
  glazewm/glazewm_state.h
  komorebi/komorebi_service.cpp
  komorebi/komorebi_service.h
  komorebi/komorebi_state.cpp
  komorebi/komorebi_state.h
  systray/icon_scale.cpp
  systray/icon_scale.h
  systray/systray_service.cpp
  systray/systray_service.h
  systray/systray_state.cpp
  systray/systray_state.h
  systray/tray_icon_registry.cpp
  systray/tray_icon_registry.h
  taskbar/pin_store.cpp
  taskbar/pin_store.h
  taskbar/taskbar_icons.cpp
  taskbar/taskbar_icons.h
  wallpapers/thumbnail_cache.cpp
  wallpapers/thumbnail_cache.h
  wallpapers/wallpaper_files.cpp
  wallpapers/wallpaper_files.h
  wallpapers/wallpaper_layout.cpp
  wallpapers/wallpaper_layout.h
  wallpapers/wallpaper_manager.cpp
  wallpapers/wallpaper_manager.h
)
target_include_directories(yasb_widgets PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_widgets
  PUBLIC yasb_ui yasb_platform nlohmann_json::nlohmann_json
  PRIVATE wabar_options wabar_kernel)
