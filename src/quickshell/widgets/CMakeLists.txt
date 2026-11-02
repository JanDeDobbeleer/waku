# The QML module `Quickshell.Widgets`: IconImage, the clipping rectangles and the wrapper types, in
# plain QML (QtQuick.Effects for the clip). WrapperManager is their shared layout, not exported.
waku_quickshell_qml_module(waku_quickshell_widgets Quickshell.Widgets
  QML_FILES
    ClippingRectangle.qml
    ClippingWrapperRectangle.qml
    IconImage.qml
    WrapperItem.qml
    WrapperMouseArea.qml
    WrapperRectangle.qml
  INTERNAL
    WrapperManager.qml)
