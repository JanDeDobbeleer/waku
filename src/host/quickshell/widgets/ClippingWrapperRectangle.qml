import QtQuick

// Quickshell.Widgets ClippingWrapperRectangle: a ClippingRectangle that sizes itself to its one
// child plus margins, the child clipped to the rounded shape. See WrapperItem for the child rules.
ClippingRectangle {
    id: root

    default property alias child: manager.child
    property alias margin: manager.margin
    property alias extraMargin: manager.extraMargin
    property alias topMargin: manager.topMargin
    property alias bottomMargin: manager.bottomMargin
    property alias leftMargin: manager.leftMargin
    property alias rightMargin: manager.rightMargin
    property alias resizeChild: manager.resizeChild

    implicitWidth: manager.contentImplicitWidth + (contentInsideBorder ? 2 * border.width : 0)
    implicitHeight: manager.contentImplicitHeight + (contentInsideBorder ? 2 * border.width : 0)

    WrapperManager {
        id: manager
        wrapper: root.contentItem
        container: root.contentItem
    }
}
