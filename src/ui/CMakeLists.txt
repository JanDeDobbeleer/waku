# ui: the shared look of widgets (Pill, Slider, Strip, DeviceMenu, DeviceRow). QML only, no C++: the files
# are compiled into the QML module waku.widgets (assembled in src/modules/), which keeps the import
# name and the resource paths (qrc:/qt/qml/waku/widgets/Pill.qml). They use the QML types of the
# host module `waku` (Theme, Tooltip, PopupWindow).
waku_contribute(
  WIDGETS_QML_FILES
    Pill.qml
    DeviceMenu.qml
    DeviceRow.qml
    Slider.qml
    Strip.qml
)
