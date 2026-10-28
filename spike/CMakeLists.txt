# Spike: Qt Quick top bar configured in QML. Throwaway; checks app bar + backdrop on a QQuickWindow,
# memory footprint, the config model and hot reload.
find_package(Qt6 6.10 REQUIRED COMPONENTS Quick Qml Network)
qt_policy(SET QTP0001 NEW)  # QML module resources under :/qt/qml/<URI>

# barnacle.services: the C++ half of the widget API (polled data sources, Process, Shell, ...).
qt_add_library(barnacle_services STATIC)
qt_add_qml_module(barnacle_services
  URI barnacle.services
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
)
target_include_directories(barnacle_services PUBLIC ${CMAKE_SOURCE_DIR}
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/services)  # generated type registration includes headers by name
target_compile_definitions(barnacle_services PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(barnacle_services PUBLIC Qt6::Qml yasb_platform yasb_widgets)

# barnacle_control: the pipe between a running bar and barnaclec.
add_subdirectory(control)

# barnacle: Config, Bar, BarLayout and the host.
add_subdirectory(bar)

# barnacle.widgets: the built-in widgets.
add_subdirectory(widgets)

qt_add_executable(barnacle-spike WIN32 main.cpp)
target_compile_definitions(barnacle-spike PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  SPIKE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(barnacle-spike PRIVATE
  barnacle_bar barnacle_barplugin barnacle_widgets barnacle_widgetsplugin
  barnacle_services barnacle_servicesplugin)
set_target_properties(barnacle-spike PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# barnaclec: the console client (reload, status, quit).
add_executable(barnaclec-spike cli/main.cpp)
target_compile_definitions(barnaclec-spike PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  SPIKE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(barnaclec-spike PRIVATE barnacle_control)
set_target_properties(barnaclec-spike PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
