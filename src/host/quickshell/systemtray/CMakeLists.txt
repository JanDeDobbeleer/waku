# The QML module `Quickshell.Services.SystemTray`: SystemTray over the notification-area service.
# SystemTrayItem is C++ (qml/) because QML cannot declare the property `id`; Status is its enum.
wabar_quickshell_qml_module(wabar_quickshell_systemtray Quickshell.Services.SystemTray
  SINGLETONS
    SystemTray.qml
  SOURCES
    qml/system_tray_item.h
    qml/system_tray_item.cpp
  DEPENDENCIES wabar wabar.services)
