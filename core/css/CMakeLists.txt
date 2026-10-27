# css: stylesheet preprocessor, rule matching and transitions. Headers need Qt Core only.
add_library(yasb_css STATIC
  preprocessor.h
  preprocessor.cpp
  rules.h
  rules.cpp
  transition_engine.h
  transition_engine.cpp
)
target_include_directories(yasb_css PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(yasb_css
  PUBLIC Qt6::Core
  PRIVATE yasb_options Qt6::Gui Qt6::Widgets)
