# Wi-Fi, Ethernet and VPN state, Wi-Fi scan and connect, the Wi-Fi radio switch, VPN control,
# interface throughput, and the Wifi widget. The Wifi widget opens
# the Windows network settings through Shell (QML only). StatSource comes from kernel/qml.
waku_feature(NAME network
  DEPENDS quickshell_io
  SOURCES
    connectivity.h
    radio.h
    throughput.cpp
    throughput.h
    vpn.h
    wlan.h
  WIN32_SOURCES
    win32/connectivity_win.cpp
    win32/network_pool.cpp
    win32/network_pool.h
    win32/radio_win.cpp
    win32/throughput_win.cpp
    win32/vpn_win.cpp
    win32/wlan_win.cpp
  QML_SOURCES
    qml/network_service.cpp
    qml/network_service.h
  QML_FILES
    qml/Wifi.qml
  TESTS
    tests/test_connectivity.cpp
    tests/test_network.cpp
    tests/test_throughput.cpp
  SYSTEM_LIBS iphlpapi rasapi32 wlanapi windowsapp
  WINRT_PCH winrt/base.h winrt/Windows.Foundation.h winrt/Windows.Foundation.Collections.h)
