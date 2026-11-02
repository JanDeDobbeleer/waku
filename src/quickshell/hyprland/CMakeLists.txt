# The QML module `Quickshell.Hyprland`: the type names of Quickshell's Hyprland module, so shells
# written for it load. Windows has no Hyprland: the lists are empty, the focused objects null and
# dispatch does nothing. GlobalShortcut is the Quickshell one; HyprlandFocusGrab ends on
# loss of the window activation.
waku_quickshell_qml_module(waku_quickshell_hyprland Quickshell.Hyprland
  QML_FILES
    GlobalShortcut.qml
    HyprlandEvent.qml
    HyprlandFocusGrab.qml
    HyprlandMonitor.qml
    HyprlandToplevel.qml
    HyprlandWorkspace.qml
  SINGLETONS
    Hyprland.qml
  DEPENDENCIES waku)
