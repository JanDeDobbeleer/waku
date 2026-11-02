import waku as W

// Quickshell's PopupWindow: a bare window next to an anchor (anchor.item, anchor.rect,
// anchor.edges, anchor.gravity, open(), close(), toggle()). It draws nothing of its own and lays out
// nothing: what you put in it fills the window as in any Window, so `anchors.fill: parent` works.
// Size it with implicitWidth and implicitHeight; unset, it takes the bounds of its content.
// `color` is transparent until set. waku's themed popup, with a surface and a column, is
// waku.PopupWindow.
W.PopupWindowBase {
    contentWidth: contentItem.childrenRect.width
    contentHeight: contentItem.childrenRect.height
}
