import QtQuick

// Quickshell.Widgets WrapperMouseArea: a MouseArea that sizes itself to its one child plus margins.
// See WrapperItem for the child rules.
MouseArea {
    id: root

    default property alias child: manager.child
    property alias margin: manager.margin
    property alias extraMargin: manager.extraMargin
    property alias topMargin: manager.topMargin
    property alias bottomMargin: manager.bottomMargin
    property alias leftMargin: manager.leftMargin
    property alias rightMargin: manager.rightMargin
    property alias resizeChild: manager.resizeChild

    implicitWidth: manager.contentImplicitWidth
    implicitHeight: manager.contentImplicitHeight

    WrapperManager {
        id: manager
        wrapper: root
    }
}
