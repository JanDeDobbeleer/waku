# barnacle.widgets: the built-in widgets, plain QML on top of barnacle.services. Inside the module
# the services are imported under the name S because the widgets share names with them (Cpu,
# Memory, Battery, Media, Workspaces).
qt_add_library(barnacle_widgets STATIC)
qt_add_qml_module(barnacle_widgets
  URI barnacle.widgets
  VERSION 1.0
  QML_FILES
    Pill.qml
    Slider.qml
    Calendar.qml
    Clock.qml
    Cpu.qml
    Memory.qml
    Battery.qml
    Volume.qml
    Media.qml
    Workspaces.qml
    SafeConfig.qml
)
target_link_libraries(barnacle_widgets PUBLIC Qt6::Quick Qt6::Qml PRIVATE barnacle_bar barnacle_services)
