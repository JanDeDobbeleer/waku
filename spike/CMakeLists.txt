# Spike: Qt Quick top bar configured in QML. Throwaway; checks app bar + backdrop on a QQuickWindow,
# memory footprint, the config model and hot reload.

# wabar.services: the C++ half of the widget API (polled data sources, Process, Shell, ...). The
# module target itself is defined in src/modules/ from these and the src/features/ contributions.
wabar_contribute(
  SERVICES_SOURCES
    services/stat_source.h
    services/system_stats.h
    services/system_stats.cpp
    services/child_process.h
    services/child_process.cpp
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
    services/network_service.h
    services/network_service.cpp
    services/taskbar_service.h
    services/taskbar_service.cpp
    services/taskbar_model.h
    services/taskbar_model.cpp
    services/control_center_service.h
    services/control_center_service.cpp
  # The generated type registration includes headers by name.
  SERVICES_INCLUDE_DIRS services
  SERVICES_LIBS wabar_audio wabar_bluetooth wabar_brightness wabar_notifications yasb_platform yasb_widgets wabar_control wabar_kernel
)

# wabar_control: the pipe between a running bar and wabarc.
add_subdirectory(control)

# wabar: Config, Bar, BarLayout and the host.
add_subdirectory(bar)

# wabar.widgets: the built-in widgets (contributed; the module is defined in src/modules/).
add_subdirectory(widgets)

qt_add_executable(wabar WIN32 main.cpp)
target_compile_definitions(wabar PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  SPIKE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(wabar PRIVATE
  wabar_bar wabar_barplugin wabar_widgets wabar_widgetsplugin
  wabar_services wabar_servicesplugin)
set_target_properties(wabar PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# wabarc: the console client (reload, status, quit).
add_executable(wabarc cli/main.cpp)
target_compile_definitions(wabarc PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  SPIKE_CONFIG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config")
target_link_libraries(wabarc PRIVATE wabar_control)
set_target_properties(wabarc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
