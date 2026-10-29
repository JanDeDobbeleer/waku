# bluetooth: radio, paired devices, battery, connect; the Bluetooth widget.
# The merge adds `DEPENDS shell` (Bluetooth.qml calls S.Shell.open).
wabar_feature(NAME bluetooth
  SOURCES
    bluetooth.h
  WIN32_SOURCES
    win32/bluetooth_win.cpp
  QML_SOURCES
    qml/bluetooth_service.h
    qml/bluetooth_service.cpp
  QML_FILES
    qml/Bluetooth.qml
  TESTS
    tests/test_bluetooth.cpp
    tests/test_bluetooth_types.cpp
  SYSTEM_LIBS bthprops cfgmgr32 setupapi ole32 uuid winmm windowsapp)
