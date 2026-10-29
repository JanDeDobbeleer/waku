import QtQuick

// The usual content of a Bar: three groups of widgets, start, centre and end.
//
//     layout: BarLayout {
//         start:  [ Clock {}, Workspaces {} ]
//         center: [ Title {} ]
//         end:    [ Volume {}, Battery {} ]
//     }
//
// A widget that sets `visible: false` takes no space. `spacing` (default Theme.spacing),
// `padding` and its per-edge forms `startPadding`/`endPadding` are in pixels.
//
// With Theme.islands each non-empty group sits on an island: a background of Theme.surface (or the
// Bar's `background`) with rounded bottom corners and concave fillets at the screen edge, as in
// Python YASB's adaptive bar. An island is the group's row plus Theme.groupPadding on each side.
Item {
    id: root

    property int spacing: Theme.spacing
    property int padding: 12
    // The room before the first start widget and after the last end widget; they follow `padding`.
    property int startPadding: padding
    property int endPadding: padding

    // One island: an item with the body's geometry (named, so tests find it) and a canvas that also
    // draws the fillets outside the body.
    component Island: Item {
        id: island

        required property Item row
        readonly property bool flipped: Window.window && Window.window.spec
                                        && Window.window.spec.position === Bar.Bottom
        readonly property color fillColor: Window.window && Window.window.spec
                                           && Window.window.spec.hasBackground
                                           ? Window.window.spec.background : Theme.surface

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
            sourceComponent: canvasComponent
        }

        Component {
            id: canvasComponent
        Canvas {
            id: canvas
            // The fillets reach this far past each side
            readonly property real pad: Theme.islandRadius
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPadChanged: requestPaint()
            Connections {
                target: island
                function onFillColorChanged() { canvas.requestPaint() }
                function onFlippedChanged() { canvas.requestPaint() }
                function onXChanged() { canvas.requestPaint() }
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const h = height
                const w = width - 2 * pad
                if (w <= 0 || h <= 0) return
                const x0 = pad
                const x1 = pad + w
                const leftFlush = island.x <= 0
                const rightFlush = island.x + island.width >= island.parent.width
                const c = Math.min(Theme.islandRadius, w / 2, h / 2)
                const f = Math.min(Theme.islandRadius, h / 2)
                if (island.flipped) {
                    ctx.translate(0, h)
                    ctx.scale(1, -1)
                }
                ctx.fillStyle = island.fillColor
                ctx.beginPath()
                if (leftFlush) {
                    ctx.moveTo(x0, 0)
                    ctx.lineTo(x0, h)
                } else {
                    ctx.moveTo(x0 - f, 0)
                    ctx.arc(x0 - f, f, f, -Math.PI / 2, 0, false)
                    ctx.lineTo(x0, h - c)
                    ctx.arc(x0 + c, h - c, c, Math.PI, Math.PI / 2, true)
                }
                if (rightFlush) {
                    ctx.lineTo(x1, h)
                    ctx.lineTo(x1, 0)
                } else {
                    ctx.lineTo(x1 - c, h)
                    ctx.arc(x1 - c, h - c, c, Math.PI / 2, 0, true)
                    ctx.lineTo(x1, f)
                    ctx.arc(x1 + f, f, f, Math.PI, 3 * Math.PI / 2, false)
                }
                ctx.closePath()
                ctx.fill()
            }
        }
        }
    }

    Island { objectName: "island-start"; row: leftRow }
    Island { objectName: "island-center"; row: centerRow }
    Island { objectName: "island-end"; row: rightRow }

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
