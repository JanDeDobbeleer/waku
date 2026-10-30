# wabar.exe: the composition root (main.cpp) and SafeShell.qml, the shell of --safe mode, which is
# compiled into the QML module wabar.widgets like the features' widgets.
wabar_contribute(WIDGETS_QML_FILES SafeShell.qml)

qt_add_executable(wabar WIN32 main.cpp quickshell_glue.h)
# WABAR_SAMPLE_CONFIG_DIR: the config home of a development build that has no --config-home and no
# WABAR_CONFIG_HOME (see resolveConfigHome in control/control.h).
target_compile_definitions(wabar PRIVATE
  WABAR_SAMPLE_CONFIG_DIR="${PROJECT_SOURCE_DIR}/examples/config")
# The static QML plugins of the modules are linked so that `import wabar` and `import Quickshell`
# resolve.
target_link_libraries(wabar PRIVATE
  wabar_options
  wabar_host wabar_hostplugin wabar_widgets wabar_widgetsplugin
  wabar_services wabar_servicesplugin wabar_quickshell_services
  wabar_quickshell wabar_quickshellplugin wabar_quickshell_wayland wabar_quickshell_waylandplugin)
set_target_properties(wabar PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
