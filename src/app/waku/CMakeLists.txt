# waku.exe: the composition root (main.cpp) and SafeShell.qml, the shell of --safe mode, which is
# compiled into the QML module waku.widgets like the features' widgets.
waku_contribute(WIDGETS_QML_FILES SafeShell.qml)

qt_add_executable(waku WIN32 main.cpp quickshell_glue.h)
# WAKU_SAMPLE_CONFIG_DIR: the config home of a development build that has no --config-home and no
# WAKU_CONFIG_HOME (see resolveConfigHome in control/control.h).
target_compile_definitions(waku PRIVATE
  WAKU_SAMPLE_CONFIG_DIR="${PROJECT_SOURCE_DIR}/examples/config")
# The static QML plugins of the modules are linked so that `import waku` and `import Quickshell`
# (and every Quickshell.* module, through waku_quickshell_modules) resolve.
target_link_libraries(waku PRIVATE
  waku_options
  waku_host waku_hostplugin waku_widgets waku_widgetsplugin
  waku_services waku_servicesplugin waku_quickshell_modules)
set_target_properties(waku PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
