# ui: the shared look of widgets (Pill, Slider, Strip, DeviceMenu, DeviceRow). QML only, no C++: the files
# are compiled into the QML module wabar.widgets (assembled in src/modules/), which keeps the import
# name and the resource paths (qrc:/qt/qml/wabar/widgets/Pill.qml). They use the QML types of the
# host module `wabar` (Theme, Tooltip, PopupWindow).
wabar_contribute(
  WIDGETS_QML_FILES
    Pill.qml
    DeviceMenu.qml
    DeviceRow.qml
    Slider.qml
    Strip.qml
)
