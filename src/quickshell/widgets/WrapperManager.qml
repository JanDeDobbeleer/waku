import QtQuick

// The layout shared by WrapperItem, WrapperRectangle, WrapperMouseArea and
// ClippingWrapperRectangle: one `child` placed inside `wrapper` with margins. The child is
// reparented to `container`; its x, y, width and height are bound here, so a child sets
// implicitWidth and implicitHeight instead. Internal to the module.
QtObject {
    id: root

    // The item whose size the child follows.
    required property Item wrapper
    // The parent the child gets (the wrapper itself unless it has an inner content item).
    property Item container: wrapper
    property Item child: null
    // Added to every side by wrappers that draw a border the child sits inside.
    property real inset: 0

    property real margin: 0
    property real extraMargin: 0
    property real topMargin: margin
    property real bottomMargin: margin
    property real leftMargin: margin
    property real rightMargin: margin
    property bool resizeChild: true

    readonly property real left: leftMargin + extraMargin + inset
    readonly property real right: rightMargin + extraMargin + inset
    readonly property real top: topMargin + extraMargin + inset
    readonly property real bottom: bottomMargin + extraMargin + inset

    readonly property real childWidth: child ? child.implicitWidth : 0
    readonly property real childHeight: child ? child.implicitHeight : 0
    readonly property real contentImplicitWidth: childWidth + left + right
    readonly property real contentImplicitHeight: childHeight + top + bottom

    onChildChanged: {
        if (child)
            child.parent = container;
    }

    readonly property list<Binding> bindings: [
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
