import QtQuick
import QtQuick.Shapes

// The usual content of a panel: three groups of widgets, start, centre and end.
//
//     content: PanelLayout {
//         start:  [ Clock {}, Workspaces {} ]
//         center: [ Title {} ]
//         end:    [ Volume {}, Battery {} ]
//     }
//
// A widget that sets `visible: false` takes no space. `spacing` (default Theme.spacing),
// `padding` and its per-edge forms `startPadding`/`endPadding` are in pixels.
//
// With Theme.islands each non-empty group sits on an island: a background of Theme.surface (or the
// window's `background`) with rounded bottom corners and concave fillets at the screen edge, as in
// Python YASB's adaptive bar. An island is the group's row plus Theme.groupPadding on each side.
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
        // The shell window it is in: islands of a bottom panel open upwards.
        readonly property var shellWindow: QsWindow.window
        readonly property bool flipped: shellWindow !== null && shellWindow.anchors !== undefined
                                        && shellWindow.anchors.bottom && !shellWindow.anchors.top
        readonly property color fillColor: shellWindow !== null && shellWindow.hasBackground
                                           ? shellWindow.background : Theme.surface

        visible: Theme.islands && row.width > 0
        x: row.x - Theme.groupPadding
        y: 0
        width: row.width + 2 * Theme.groupPadding
        height: parent ? parent.height : 0

        Loader {
            active: island.visible
            x: -Theme.islandRadius
            width: parent.width + 2 * Theme.islandRadius
            height: parent.height
            sourceComponent: shapeComponent
        }

        Component {
            id: shapeComponent
            // A scene graph path, not a JS canvas: no repaint step and no texture upload when a
            // widget changes width.
            Shape {
                id: shape
                // The fillets reach this far past each side
                readonly property real pad: Theme.islandRadius
                readonly property real x0: pad
                readonly property real x1: width - pad
                readonly property real c: Math.max(0, Math.min(Theme.islandRadius, (x1 - x0) / 2, height / 2))
                readonly property real f: Math.max(0, Math.min(Theme.islandRadius, height / 2))
                readonly property bool leftFlush: island.x <= 0
                readonly property bool rightFlush: island.x + island.width >= island.parent.width
                readonly property string outline: {
                    const h = height
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

                anchors.fill: parent
                visible: x1 > x0 && height > 0
                preferredRendererType: Shape.CurveRenderer
                transform: Scale {
                    origin.y: shape.height / 2
                    yScale: island.flipped ? -1 : 1
                }

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

    Island { objectName: "island-start"; row: leftRow; stops: root.startStops }
    Island { objectName: "island-center"; row: centerRow; stops: root.centerStops }
    Island { objectName: "island-end"; row: rightRow; stops: root.endStops }

    // The widgets are parented straight into the rows, so they behave like ordinary children.
    property alias start: leftRow.data
    property alias center: centerRow.data
    property alias end: rightRow.data

    Row {
        id: leftRow
        anchors.left: parent.left
        anchors.leftMargin: root.startPadding
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.spacing
    }

    Row {
        id: centerRow
        anchors.centerIn: parent
        spacing: root.spacing
    }

    Row {
        id: rightRow
        anchors.right: parent.right
        anchors.rightMargin: root.endPadding
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.spacing
    }
}
