import QtQuick

// Quickshell.Widgets WrapperRectangle: a Rectangle that sizes itself to its one child plus margins.
// With contentInsideBorder (default true) the border width is added to the margins. See
// WrapperItem for the child rules.
Rectangle {
    id: root

    default property alias child: manager.child
    property alias margin: manager.margin
    property alias extraMargin: manager.extraMargin
    property alias topMargin: manager.topMargin
    property alias bottomMargin: manager.bottomMargin
    property alias leftMargin: manager.leftMargin
    property alias rightMargin: manager.rightMargin
    property alias resizeChild: manager.resizeChild
    property bool contentInsideBorder: true

    implicitWidth: manager.contentImplicitWidth
    implicitHeight: manager.contentImplicitHeight

    WrapperManager {
        id: manager
        wrapper: root
        inset: root.contentInsideBorder ? root.border.width : 0
    }
}
