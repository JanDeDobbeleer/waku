# tidy.widgets: the built-in widgets, plain QML on top of tidy.services. Inside the module
# the services are imported under the name S because the widgets share names with them (Cpu,
# Memory, Battery, Media, Workspaces).
qt_add_library(tidy_widgets STATIC)
qt_add_qml_module(tidy_widgets
  URI tidy.widgets
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
    TrayIconButton.qml
    Tray.qml
    SafeConfig.qml
)
target_link_libraries(tidy_widgets PUBLIC Qt6::Quick Qt6::Qml PRIVATE tidy_bar tidy_services)
