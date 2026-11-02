# bluetooth: radio, paired devices, battery, connect; the Bluetooth widget.
waku_feature(NAME bluetooth
  DEPENDS quickshell_io
  SOURCES
    bluetooth.h
  WIN32_SOURCES
    win32/bluetooth_audio_win.h
    win32/bluetooth_audio_win.cpp
    win32/bluetooth_types_win.cpp
    win32/bluetooth_win.cpp
  QML_SOURCES
    qml/bluetooth_service.h
    qml/bluetooth_service.cpp
  QML_FILES
    qml/Bluetooth.qml
  TESTS
    tests/test_bluetooth.cpp
    tests/test_bluetooth_scan.cpp
    tests/test_bluetooth_types.cpp
  SYSTEM_LIBS bthprops cfgmgr32 setupapi ole32 uuid winmm windowsapp)
