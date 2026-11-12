import QtQuick

// Quickshell.Widgets WrapperMouseArea: a MouseArea that sizes itself to its one child plus margins.
// See WrapperItem for the child rules.
MouseArea {
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
