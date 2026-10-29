# wabar.widgets: the built-in widgets, plain QML on top of wabar.services. Inside the module
# the services are imported under the name S because the widgets share names with them (Cpu,
# Memory, Battery, Media, Workspaces).
# The module target is defined in src/modules/ from these and the src/features/ contributions.
wabar_contribute(
  WIDGETS_QML_FILES
    Pill.qml
    DeviceMenu.qml
    DeviceRow.qml
    Slider.qml
    Calendar.qml
    Clock.qml
    Cpu.qml
    Memory.qml
    Battery.qml
    Media.qml
    Workspaces.qml
    WorkspaceDot.qml
    TrayIconButton.qml
    Tray.qml
    ActiveLayout.qml
    Bluetooth.qml
    Notifications.qml
    ControlCenter.qml
    SafeConfig.qml
)
