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
    Volume.qml
    Bluetooth.qml
    Notifications.qml
    ControlCenter.qml
    SafeConfig.qml
)
