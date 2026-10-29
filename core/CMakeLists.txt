# ui: the app bar (appbar registration), the DWM backdrop helpers and the event bus header.
add_library(yasb_ui STATIC
  bar/app_bar.h
  bar/app_bar.cpp
  widgets/backdrop.h
  widgets/backdrop.cpp
  widgets/event_bus.h
)
target_include_directories(yasb_ui PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_ui
  PUBLIC Qt6::Core Qt6::Gui
  PRIVATE wabar_options wabar_kernel)
