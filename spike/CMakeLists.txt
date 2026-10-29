# Spike: Qt Quick top bar configured in QML. Throwaway; checks app bar + backdrop on a QQuickWindow,
# memory footprint, the config model and hot reload.
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick Qml Network)
qt_policy(SET QTP0001 NEW)  # QML module resources under :/qt/qml/<URI>

# tidy.services: the C++ half of the widget API (polled data sources, Process, Shell, ...).
qt_add_library(tidy_services STATIC)
qt_add_qml_module(tidy_services
  URI tidy.services
  VERSION 1.0
  SOURCES
    services/stat_source.h
    services/stat_source.cpp
    services/system_stats.h
    services/system_stats.cpp
    services/child_process.h
    services/child_process.cpp
    services/audio_service.h
    services/audio_service.cpp
    services/media_service.h
    services/media_service.cpp
    services/glazewm_client.h
    services/glazewm_client.cpp
    services/komorebi_client.h
    services/komorebi_client.cpp
    services/shell_service.h
    services/shell_service.cpp
    services/workspaces_service.h
    services/workspaces_service.cpp
    services/tray_icon_model.h
    services/tray_icon_model.cpp
    services/tray_image_provider.h
    services/tray_image_provider.cpp
    services/systray.h
    services/systray.cpp
    services/tray_pins.h
    services/tray_pins.cpp
)
target_include_directories(tidy_services PUBLIC ${CMAKE_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/services)  # generated type registration includes headers by name
target_compile_definitions(tidy_services PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(tidy_services PUBLIC Qt6::Qml Qt6::Quick yasb_platform yasb_widgets)

# tidy_control: the pipe between a running bar and tidybarc.
add_subdirectory(control)

# tidy: Config, Bar, BarLayout and the host.
add_subdirectory(bar)

# tidy.widgets: the built-in widgets.
add_subdirectory(widgets)

qt_add_executable(tidybar WIN32 main.cpp)
target_compile_definitions(tidybar PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  SPIKE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(tidybar PRIVATE
  tidy_bar tidy_barplugin tidy_widgets tidy_widgetsplugin
  tidy_services tidy_servicesplugin)
set_target_properties(tidybar PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# tidybarc: the console client (reload, status, quit).
add_executable(tidybarc cli/main.cpp)
target_compile_definitions(tidybarc PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  SPIKE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(tidybarc PRIVATE tidy_control)
set_target_properties(tidybarc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
