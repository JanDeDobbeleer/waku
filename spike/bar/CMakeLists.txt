# barnacle: the QML module a config imports (Config, Bar, BarLayout) and the host that builds
# windows from it.
qt_add_library(barnacle_bar STATIC)
qt_add_qml_module(barnacle_bar
  URI barnacle
  VERSION 1.0
  QML_FILES
    BarLayout.qml
    BarWindow.qml
    FallbackConfig.qml
  SOURCES
    bar_config.h
    bar_theme.h
    bar_theme.cpp
    screen_match.h
    screen_match.cpp
    bar_host.h
    bar_host.cpp
    bar_host_win32.cpp
)
# The generated type registration includes headers by name, hence the private include directory.
target_include_directories(barnacle_bar PUBLIC ${CMAKE_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(barnacle_bar PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(barnacle_bar PUBLIC Qt6::Quick Qt6::Qml yasb_ui)
