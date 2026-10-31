# The QML module `Quickshell.Wayland`: the layer-shell vocabulary ported shells use on a
# PanelWindow (`WlrLayershell.layer`, `.keyboardFocus`, `.namespace`, `.exclusionMode`) and its
# enums, mapped onto the PanelWindow's own state. Registrations only (qml/wayland_types.h).
qt_add_library(wabar_quickshell_wayland STATIC)
qt_add_qml_module(wabar_quickshell_wayland
  URI Quickshell.Wayland
  VERSION 0.1
  NO_GENERATE_EXTRA_QMLDIRS
  SOURCES qml/wayland_types.h
)
target_include_directories(wabar_quickshell_wayland PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qml)
target_link_libraries(wabar_quickshell_wayland
  PUBLIC Qt6::Quick Qt6::Qml wabar_host
  PRIVATE wabar_options)
wabar_qt_generated_code(wabar_quickshell_wayland)
