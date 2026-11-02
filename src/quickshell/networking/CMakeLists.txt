# The QML module `Quickshell.Networking` (a noctalia-qs addition): the Wi-Fi radio switch over the
# Network service.
waku_quickshell_qml_module(waku_quickshell_networking Quickshell.Networking
  SINGLETONS
    Networking.qml
  DEPENDENCIES waku.services)
