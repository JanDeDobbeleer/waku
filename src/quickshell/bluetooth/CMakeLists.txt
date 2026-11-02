# The QML module `Quickshell.Bluetooth`: Bluetooth, BluetoothAdapter and BluetoothDevice over the
# Bluetooth service (radio switch, known devices, connect and disconnect).
waku_quickshell_qml_module(waku_quickshell_bluetooth Quickshell.Bluetooth
  QML_FILES
    BluetoothAdapter.qml
    BluetoothDevice.qml
  SINGLETONS
    Bluetooth.qml
    BluetoothAdapterState.qml
    BluetoothDeviceState.qml
  DEPENDENCIES waku waku.services)
