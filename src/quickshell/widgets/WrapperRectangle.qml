import QtQuick

// Quickshell.Widgets WrapperRectangle: a Rectangle that sizes itself to its one child plus margins.
// With contentInsideBorder (default true) the border width is added to the margins. See
// WrapperItem for the child rules.
Rectangle {
    id: root

    default final property alias child: manager.child
    final property alias margin: manager.margin
    final property alias extraMargin: manager.extraMargin
    final property alias topMargin: manager.topMargin
    final property alias bottomMargin: manager.bottomMargin
    final property alias leftMargin: manager.leftMargin
    final property alias rightMargin: manager.rightMargin
    final property alias resizeChild: manager.resizeChild
    final property bool contentInsideBorder: true

    implicitWidth: manager.contentImplicitWidth
    implicitHeight: manager.contentImplicitHeight

    WrapperManager {
        id: manager
        wrapper: root
        inset: root.contentInsideBorder ? root.border.width : 0
    }
}
