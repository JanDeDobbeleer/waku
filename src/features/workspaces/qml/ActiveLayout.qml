import QtQuick
import QtQuick.Window
import waku
import waku.services as S

// The komorebi tiling layout of this bar's monitor. A click switches to the next layout of
// `layouts`, the wheel steps through them; while the workspace is paused, floating, maximized or
// in monocle, a click undoes that state instead. Hovering shows the layout name. Hidden when
// komorebi is not running.
//
//     ActiveLayout { layouts: ["bsp", "columns", "grid"]; format: "layout: %1"; labels: ({ bsp: "BSP" }) }
//
// `format`: %1 is the layout text. `labels` maps a layout (bsp, columns, rows, grid, scrolling,
// vertical_stack, horizontal_stack, ultrawide_vertical_stack, right_main_vertical_stack, paused,
// floating, maximized, monocle) to its text; layouts without an entry show a readable name such as
// "Vertical Stack".
Pill {
    id: root

    property var layouts: ["bsp", "columns", "rows", "grid", "scrolling", "vertical_stack",
                           "horizontal_stack", "ultrawide_vertical_stack",
                           "right_main_vertical_stack"]
    property string format: "%1"
    property var labels: ({})

    // HMONITOR of the bar this widget is in (set on the bar's window by the host).
    readonly property real handle: QsWindow.window ? QsWindow.window.monitorHandle : 0
    readonly property var monitor: {
        for (const m of S.Komorebi.monitors)
            if (m.id === handle) return m;
        return null;
    }
    readonly property string layout: monitor ? monitor.layout : ""
    readonly property string layoutName: {
        if (labels && labels[layout] !== undefined)
            return labels[layout];
        return layout.split("_").map(w => w.charAt(0).toUpperCase() + w.slice(1)).join(" ");
    }

    visible: S.Komorebi.connected && monitor !== null && layout !== ""
    text: format.replace("%1", layoutName)
    tooltip: "Layout: " + layoutName

    onClicked: step(1)
    onScrolled: delta => step(delta > 0 ? -1 : 1)

    function step(by) {
        if (monitor)
            S.Komorebi.cycleLayout(monitor.index, layouts, by);
    }
}
