# The QML module `Quickshell.Wayland`: the layer-shell vocabulary ported shells use on a
# PanelWindow (`WlrLayershell.layer`, `.keyboardFocus`, `.namespace`, `.exclusionMode`) and its
# enums, mapped onto the PanelWindow's own state. Registrations only (qml/wayland_types.h).
wabar_quickshell_qml_module(wabar_quickshell_wayland Quickshell.Wayland
  SOURCES qml/wayland_types.h)
