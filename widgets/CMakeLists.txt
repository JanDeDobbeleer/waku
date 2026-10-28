# widgets: concrete widgets and their registration (registerStatWidgets, called from app/main.cpp).
add_library(yasb_widgets STATIC
  network_widgets.h
  network_widgets.cpp
  stat_widgets.h
  stat_widgets.cpp
  system_widget_helpers.h
  battery/battery_widget.h
  battery/battery_widget.cpp
  cpu/cpu_widget.h
  clock/clock_dialogs.cpp
  clock/clock_state.cpp
  clock/clock_state.h
  clock/clock_widget.cpp
  clock/clock_widget.h
  clock/py_strftime.cpp
  clock/py_strftime.h
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
  menu_support/elided_label.h
  menu_support/label_parts.h
  menu_support/loader_line.h
  menu_support/loader_line.cpp
  menu_support/menu_popup.h
  menu_support/menu_popup.cpp
  wifi/wifi_menu.h
  wifi/wifi_menu.cpp
  wifi/wifi_widget.h
  wifi/wifi_widget.cpp
  bluetooth/bluetooth_menu.h
  bluetooth/bluetooth_menu.cpp
  bluetooth/bluetooth_widget.h
  bluetooth/bluetooth_widget.cpp
)
set_target_properties(yasb_widgets PROPERTIES AUTORCC ON)
target_include_directories(yasb_widgets PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_widgets
  PUBLIC yasb_ui yasb_platform
  PRIVATE yasb_options)
