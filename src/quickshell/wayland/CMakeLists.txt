# The QML module `Quickshell.Wayland`: the layer-shell vocabulary ported shells use on a
# PanelWindow (`WlrLayershell.layer`, `.keyboardFocus`, `.namespace`, `.exclusionMode`) and its
# enums, mapped onto the PanelWindow's own state (qml/wayland_types.h), IdleInhibitor from the
# desktop_state feature, IdleMonitor over the idle time (IdleMonitor.qml), and ToplevelManager and
# Toplevel over the taskbar service.
waku_quickshell_qml_module(waku_quickshell_wayland Quickshell.Wayland
  QML_FILES
    IdleMonitor.qml
    Toplevel.qml
  SINGLETONS
    ToplevelManager.qml
  SOURCES qml/wayland_types.h
  DEPENDENCIES waku waku.services)
