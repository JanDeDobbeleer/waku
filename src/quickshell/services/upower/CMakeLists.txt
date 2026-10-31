# The QML module `Quickshell.Services.UPower`: UPower and PowerProfiles over the battery service
# and the Windows power modes. UPowerBase (C++, qml/) is the base of the UPower singleton, for its
# `onBattery` property (QML cannot declare a name that starts with "on").
wabar_quickshell_qml_module(wabar_quickshell_upower Quickshell.Services.UPower
  QML_FILES
    UPowerDevice.qml
  SINGLETONS
    PerformanceDegradationReason.qml
    PowerProfile.qml
    PowerProfiles.qml
    UPower.qml
    UPowerDeviceState.qml
    UPowerDeviceType.qml
  SOURCES
    qml/upower_base.h
  DEPENDENCIES wabar wabar.services)
