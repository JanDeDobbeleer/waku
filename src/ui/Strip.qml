import QtQuick
import wabar

// A positioner for the items of a widget: a Row on a horizontal panel, a Column on a panel docked
// to the left or right edge (`QsWindow.window.dockVertical`). The built-in Workspaces, Taskbar and
// Tray are Strips; use one as the root of a widget that holds several items.
//
//     Strip {
//         spacing: 4
//         Rectangle { width: 20; height: 20 }
//         Rectangle { width: 20; height: 20 }
//     }
//
// It is a Flow, so it takes `spacing` and the paddings like a Row. It does not wrap while it has no
// explicit `width` (or `height` when vertical); do not set one. A Flow does not allow `anchors` on
// its children, so items inside a Strip are placed by the Strip alone: give an item a height or
// width to align it, and wrap it in an Item to centre it.
Flow {
    id: root

    // True on a panel docked to the left or right edge.
    readonly property bool vertical: QsWindow.window !== null && QsWindow.window.dockVertical

    flow: vertical ? Flow.TopToBottom : Flow.LeftToRight
}
