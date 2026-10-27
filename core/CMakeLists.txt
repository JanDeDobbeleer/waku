# config and css sit below ui and must not depend on each other.
add_subdirectory(config)
add_subdirectory(css)

# ui: bar shell, widget framework and helpers. Sources are added by the subdirectories.
add_library(yasb_ui STATIC
  version.h
  version.cpp
)
target_include_directories(yasb_ui PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_ui
  PUBLIC yasb_css yasb_config Qt6::Core Qt6::Gui Qt6::Widgets
  PRIVATE yasb_options)

add_subdirectory(bar)
add_subdirectory(widgets)
add_subdirectory(util)
