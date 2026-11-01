pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes

// The usual content of a panel: three groups of widgets, start, centre and end. In a panel docked
// to the left or right edge (`QsWindow.window.dockVertical`) the groups run top to bottom: start
// at the top, end at the bottom.
//
//     content: PanelLayout {
//         start:  [ Clock {}, Workspaces {} ]
//         center: [ Title {} ]
//         end:    [ Volume {}, Battery {} ]
//     }
//
// A widget that sets `visible: false` takes no space. `spacing` (default Theme.spacing),
// `padding` and its per-edge forms `startPadding`/`endPadding` are in pixels; they are the room
// before the first and after the last widget along the panel.
//
// With Theme.islands each non-empty group sits on an island: a background of Theme.surface (or the
// window's `background`) with rounded inner corners and concave fillets at the screen edge, as in
// Python YASB's adaptive bar. An island is the group's strip plus Theme.groupPadding on each side.
// It is drawn once in a local frame (along the panel, depth away from the screen edge) and mapped
// onto the panel's edge, so the fillets meet the screen edge on all four sides.
//
// `startFill`, `centerFill` and `endFill` paint an island with a horizontal gradient across its
// width instead of the flat fill. Each is a list of colours, spaced evenly; one colour is flat and
// an empty list (the default) keeps the flat fill. Entries that are not colours are skipped. When
// `centerFill` is empty but both others are not, the centre island runs from the last colour of
// `startFill` to the first of `endFill`, so the three islands read as one bar (`centerStops`).
//
//     content: PanelLayout {
//         startFill: ["#ff6a00", "#e8338a", "#8a3ffc"]
//         endFill:   ["#2f6bff", "#1fb5c9"]
//     }
Item {
    id: root

    property int spacing: Theme.spacing
    property int padding: 12
    // The room before the first start widget and after the last end widget; they follow `padding`.
    property int startPadding: padding
    property int endPadding: padding

    // True in a panel docked to the left or right edge.
    readonly property bool vertical: QsWindow.window !== null && QsWindow.window.dockVertical

    property var startFill: []
    property var centerFill: []
    property var endFill: []

    // The usable colours of a fill list, as colour values; anything else gives an empty list.
    function fillStops(list) {
        const out = []
        if (!list || typeof list === "string" || list.length === undefined) return out
        for (let i = 0; i < list.length; ++i) {
            try {
                const c = Qt.color(list[i])
                if (c.valid) out.push(c)
            } catch (e) {}
        }
        return out
    }
    readonly property var startStops: fillStops(startFill)
    readonly property var endStops: fillStops(endFill)
    readonly property var centerStops: {
        const own = fillStops(centerFill)
        if (own.length > 0) return own
        if (startStops.length > 0 && endStops.length > 0)
            return [startStops[startStops.length - 1], endStops[0]]
        return []
    }

    // One island: an item with the body's geometry (named, so tests find it) and a shape that also
    // draws the fillets outside the body.
    component Island: Item {
        id: island

        required property Item row
        // Gradient colours; empty paints the flat fillColor
        property var stops: []
        // The shell window it is in: islands of a bottom panel open upwards, of a left panel to
        // the right, of a right panel to the left.
        readonly property var shellWindow: QsWindow.window
        readonly property int edge: shellWindow !== null ? shellWindow.dockEdge : DockEdge.None
        readonly property bool vertical: edge === DockEdge.Left || edge === DockEdge.Right
        readonly property color fillColor: shellWindow !== null && shellWindow.hasBackground
                                           ? shellWindow.background : Theme.surface

        visible: Theme.islands && (vertical ? row.height > 0 : row.width > 0)
        x: vertical ? 0 : row.x - Theme.groupPadding
        y: vertical ? row.y - Theme.groupPadding : 0
        width: vertical ? (parent ? parent.width : 0) : row.width + 2 * Theme.groupPadding
        height: vertical ? row.height + 2 * Theme.groupPadding : (parent ? parent.height : 0)

        Loader {
            active: island.visible
            x: island.vertical ? 0 : -Theme.islandRadius
            y: island.vertical ? -Theme.islandRadius : 0
            width: island.vertical ? parent.width : parent.width + 2 * Theme.islandRadius
            height: island.vertical ? parent.height + 2 * Theme.islandRadius : parent.height
            sourceComponent: shapeComponent
        }

        Component {
            id: shapeComponent
            // The Loader sizes this item to the island's frame on screen; the shape inside keeps
            // its own local frame.
            Item {
                // A scene graph path, not a JS canvas: no repaint step and no texture upload when a
                // widget changes width.
                Shape {
                    id: shape
                    // The local frame: `along` runs along the panel (the panel's x on a top or bottom
                    // panel, y on a left or right one), `depth` away from the screen edge, which is
                    // depth 0. The fillets reach `pad` past each end of the island.
                    readonly property real pad: Theme.islandRadius
                    readonly property real along: island.vertical ? parent.height : parent.width
                    readonly property real depth: island.vertical ? parent.width : parent.height
                    readonly property real x0: pad
                    readonly property real x1: along - pad
                    readonly property real c: Math.max(0, Math.min(Theme.islandRadius, (x1 - x0) / 2, depth / 2))
                    readonly property real f: Math.max(0, Math.min(Theme.islandRadius, depth / 2))
                    readonly property bool leftFlush: island.vertical ? island.y <= 0 : island.x <= 0
                    readonly property bool rightFlush: island.vertical
                        ? island.y + island.height >= island.parent.height
                        : island.x + island.width >= island.parent.width
                    // Local (along, depth) to the panel's own coordinates: a transpose for a left or
                    // right panel, a flip of the depth for a bottom or right one.
                    readonly property matrix4x4 mapping: {
                        switch (island.edge) {
                        case DockEdge.Bottom: return Qt.matrix4x4(1, 0, 0, 0,  0, -1, 0, depth,  0, 0, 1, 0,  0, 0, 0, 1)
                        case DockEdge.Left:   return Qt.matrix4x4(0, 1, 0, 0,  1, 0, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1)
                        case DockEdge.Right:  return Qt.matrix4x4(0, -1, 0, depth,  1, 0, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1)
                        default:              return Qt.matrix4x4(1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1)
                        }
                    }
                    readonly property string outline: {
                        const h = depth
                        let d = leftFlush
                            ? "M " + x0 + " 0 L " + x0 + " " + h
                            : "M " + (x0 - f) + " 0 A " + f + " " + f + " 0 0 1 " + x0 + " " + f
                              + " L " + x0 + " " + (h - c)
                              + " A " + c + " " + c + " 0 0 0 " + (x0 + c) + " " + h
                        d += rightFlush
                            ? " L " + x1 + " " + h + " L " + x1 + " 0"
                            : " L " + (x1 - c) + " " + h
                              + " A " + c + " " + c + " 0 0 0 " + x1 + " " + (h - c)
                              + " L " + x1 + " " + f
                              + " A " + f + " " + f + " 0 0 1 " + (x1 + f) + " 0"
                        return d + " Z"
                    }

                    width: along
                    height: depth
                    visible: x1 > x0 && depth > 0
                    preferredRendererType: Shape.CurveRenderer
                    transformOrigin: Item.TopLeft
                    transform: Matrix4x4 { matrix: shape.mapping }

                    function applyFill() {
                        const stops = island.stops
                        if (stops && stops.length > 1) {
                            let src = "import QtQuick; import QtQuick.Shapes; LinearGradient { x1: " + x0
                                      + "; y1: 0; x2: " + x1 + "; y2: 0; "
                            for (let i = 0; i < stops.length; ++i)
                                src += "GradientStop { position: " + i / (stops.length - 1)
                                       + "; color: \"" + stops[i] + "\" } "
                            const old = body.fillGradient
                            body.fillGradient = Qt.createQmlObject(src + "}", body)
                            if (old) old.destroy()
                        } else {
                            body.fillGradient = null
                            body.fillColor = stops && stops.length === 1 ? stops[0] : island.fillColor
                        }
                    }
                    Component.onCompleted: applyFill()
                    onX0Changed: applyFill()
                    onX1Changed: applyFill()
                    Connections {
                        target: island
                        function onFillColorChanged() { shape.applyFill() }
                        function onStopsChanged() { shape.applyFill() }
                    }

                    ShapePath {
                        id: body
                        strokeColor: "transparent"
                        strokeWidth: -1
                        PathSvg { path: shape.outline }
                    }
                }
            }
        }
    }

    Island { objectName: "island-start"; row: startStrip; stops: root.startStops }
    Island { objectName: "island-center"; row: centerStrip; stops: root.centerStops }
    Island { objectName: "island-end"; row: endStrip; stops: root.endStops }

    // The widgets are parented straight into the strips, so they behave like ordinary children.
    property alias start: startStrip.data
    property alias center: centerStrip.data
    property alias end: endStrip.data

    // A strip is a Flow that runs along the panel without wrapping: left to right, or top to bottom
    // in a left or right panel. (ui/Strip.qml is the same thing for widgets; this module cannot
    // import it.)
    component Group: Flow {
        flow: root.vertical ? Flow.TopToBottom : Flow.LeftToRight
        spacing: root.spacing
    }

    Group {
        id: startStrip
        anchors.left: root.vertical ? undefined : parent.left
        anchors.leftMargin: root.startPadding
        anchors.verticalCenter: root.vertical ? undefined : parent.verticalCenter
        anchors.top: root.vertical ? parent.top : undefined
        anchors.topMargin: root.startPadding
        anchors.horizontalCenter: root.vertical ? parent.horizontalCenter : undefined
    }

    Group {
        id: centerStrip
        anchors.centerIn: parent
    }

    Group {
        id: endStrip
        anchors.right: root.vertical ? undefined : parent.right
        anchors.rightMargin: root.endPadding
        anchors.verticalCenter: root.vertical ? undefined : parent.verticalCenter
        anchors.bottom: root.vertical ? parent.bottom : undefined
        anchors.bottomMargin: root.endPadding
        anchors.horizontalCenter: root.vertical ? parent.horizontalCenter : undefined
    }
}
