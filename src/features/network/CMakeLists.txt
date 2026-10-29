# Wi-Fi, Ethernet and VPN state, Wi-Fi scan and connect, and the Wifi widget. The Wifi widget opens
# the Windows network settings through Shell (QML only). StatSource comes from kernel/qml.
wabar_feature(NAME network
  DEPENDS shell
  SOURCES
    connectivity.h
    wlan.h
  WIN32_SOURCES
    win32/connectivity_win.cpp
    win32/wlan_win.cpp
  QML_SOURCES
    qml/network_service.cpp
    qml/network_service.h
  QML_FILES
    qml/Wifi.qml
  TESTS
    tests/test_connectivity.cpp
    tests/test_network.cpp
  SYSTEM_LIBS wlanapi ws2_32 windowsapp)
