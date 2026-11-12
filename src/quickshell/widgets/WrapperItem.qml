import QtQuick

// Quickshell.Widgets WrapperItem: an Item that sizes itself to its one child plus margins.
//
//     WrapperItem { margin: 4; Text { text: "hi" } }
//
// The child is the default property. Set implicitWidth and implicitHeight on the child, not x, y,
// width, height or anchors. Assigning `undefined` to a margin does not reset it to `margin`; assign
// the value instead.
Item {
    id: root

    default final property alias child: manager.child
    final property alias margin: manager.margin
    final property alias extraMargin: manager.extraMargin
    final property alias topMargin: manager.topMargin
    final property alias bottomMargin: manager.bottomMargin
    final property alias leftMargin: manager.leftMargin
    final property alias rightMargin: manager.rightMargin
    final property alias resizeChild: manager.resizeChild

    implicitWidth: manager.contentImplicitWidth
    implicitHeight: manager.contentImplicitHeight

    WrapperManager {
        id: manager
        wrapper: root
    }
}
