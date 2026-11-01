# The QML module `Quickshell.Networking` (a noctalia-qs addition): the Wi-Fi radio switch over the
# Network service.
wabar_quickshell_qml_module(wabar_quickshell_networking Quickshell.Networking
  SINGLETONS
    Networking.qml
  DEPENDENCIES wabar.services)
