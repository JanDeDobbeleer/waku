# widgets: concrete widgets and their registration (registerStatWidgets, called from app/main.cpp).
add_library(yasb_widgets STATIC
  stat_widgets.h
  stat_widgets.cpp
  system_widget_helpers.h
  battery/battery_widget.h
  battery/battery_widget.cpp
  cpu/cpu_widget.h
  cpu/cpu_widget.cpp
  github/github_auth_dialog.h
  github/github_auth_dialog.cpp
  github/github_data_store.h
  github/github_data_store.cpp
  github/github_format.h
  github/github_format.cpp
  github/github_widget.h
  github/github_widget.cpp
  memory/memory_widget.h
  memory/memory_widget.cpp
)
target_include_directories(yasb_widgets PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_widgets
  PUBLIC yasb_ui yasb_platform
  PRIVATE yasb_options)
