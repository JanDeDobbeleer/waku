import QtQuick

// The layout shared by WrapperItem, WrapperRectangle, WrapperMouseArea and
// ClippingWrapperRectangle: one `child` placed inside `wrapper` with margins. The child is
// reparented to `container`; its x, y, width and height are bound here, so a child sets
// implicitWidth and implicitHeight instead. Internal to the module.
QtObject {
    id: root

    // The item whose size the child follows.
    final required property Item wrapper
    // The parent the child gets (the wrapper itself unless it has an inner content item).
    final property Item container: wrapper
    final property Item child: null
    // Added to every side by wrappers that draw a border the child sits inside.
    final property real inset: 0

    final property real margin: 0
    final property real extraMargin: 0
    final property real topMargin: margin
    final property real bottomMargin: margin
    final property real leftMargin: margin
    final property real rightMargin: margin
    final property bool resizeChild: true

    final readonly property real left: leftMargin + extraMargin + inset
    final readonly property real right: rightMargin + extraMargin + inset
    final readonly property real top: topMargin + extraMargin + inset
    final readonly property real bottom: bottomMargin + extraMargin + inset

    final readonly property real childWidth: child ? child.implicitWidth : 0
    final readonly property real childHeight: child ? child.implicitHeight : 0
    final readonly property real contentImplicitWidth: childWidth + left + right
    final readonly property real contentImplicitHeight: childHeight + top + bottom

    onChildChanged: {
        if (child)
            child.parent = container;
    }

    final readonly property list<Binding> bindings: [
        Binding {
            target: root.child
            property: "x"
            value: root.left
            when: root.child !== null
            restoreMode: Binding.RestoreNone
        },
        Binding {
            target: root.child
            property: "y"
            value: root.top
            when: root.child !== null
            restoreMode: Binding.RestoreNone
        },
        Binding {
            target: root.child
            property: "width"
            value: root.resizeChild ? Math.max(root.childWidth, root.wrapper.width - root.left - root.right) : root.childWidth
            when: root.child !== null
            restoreMode: Binding.RestoreNone
        },
        Binding {
            target: root.child
            property: "height"
            value: root.resizeChild ? Math.max(root.childHeight, root.wrapper.height - root.top - root.bottom) : root.childHeight
            when: root.child !== null
            restoreMode: Binding.RestoreNone
        }
    ]
}
