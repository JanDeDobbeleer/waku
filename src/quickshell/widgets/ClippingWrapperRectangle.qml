import QtQuick

// Quickshell.Widgets ClippingWrapperRectangle: a ClippingRectangle that sizes itself to its one
// child plus margins, the child clipped to the rounded shape. See WrapperItem for the child rules.
ClippingRectangle {
    id: root

    default final property alias child: manager.child
    final property alias margin: manager.margin
    final property alias extraMargin: manager.extraMargin
    final property alias topMargin: manager.topMargin
    final property alias bottomMargin: manager.bottomMargin
    final property alias leftMargin: manager.leftMargin
    final property alias rightMargin: manager.rightMargin
    final property alias resizeChild: manager.resizeChild

    implicitWidth: manager.contentImplicitWidth + (contentInsideBorder ? 2 * border.width : 0)
    implicitHeight: manager.contentImplicitHeight + (contentInsideBorder ? 2 * border.width : 0)

    WrapperManager {
        id: manager
        wrapper: root.contentItem
        container: root.contentItem
    }
}
