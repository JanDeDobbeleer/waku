pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import wabar

// A rolling area chart of percentages (0 to 100), the graph of the CPU and memory popups.
//
//     StatGraph { values: cpu.history; color: Theme.accent; grid: true }
//
// `values` is oldest first and spread over the full width. The line is a smoothed spline; below it
// a gradient fades from the line colour. The grid is square cells of `cell` pixels anchored at the
// bottom right. Nothing is drawn for fewer than two values.
Item {
    id: root

    property var values: []
    property color color: Theme.accent
    property bool grid: true
    property real cell: 16
    property real strokeWidth: 2
    property real tension: 0.2

    // The points as [x, y] pairs; also what the tests read.
    readonly property var points: {
        const n = values.length;
        if (n < 2 || width <= 0 || height <= 0)
            return [];
        const pad = strokeWidth / 2;
        const chartHeight = height - 2 * pad;
        const step = width / (n - 1);
        const out = [];
        for (let i = 0; i < n; ++i) {
            const frac = Math.max(0, Math.min(values[i] / 100, 1));
            out.push([i * step, pad + chartHeight * (1 - frac)]);
        }
        return out;
    }

    // SVG path commands of the smoothed line (without the leading move).
    readonly property string curve: {
        const pts = points;
        const last = pts.length - 1;
        let d = "";
        for (let i = 0; i < last; ++i) {
            const p0 = pts[Math.max(0, i - 1)];
            const p1 = pts[i];
            const p2 = pts[i + 1];
            const p3 = pts[Math.min(last, i + 2)];
            const lo = Math.min(p1[1], p2[1]);
            const hi = Math.max(p1[1], p2[1]);
            const c1y = Math.max(lo, Math.min(p1[1] + (p2[1] - p0[1]) * tension, hi));
            const c2y = Math.max(lo, Math.min(p2[1] - (p3[1] - p1[1]) * tension, hi));
            d += " C " + (p1[0] + (p2[0] - p0[0]) * tension) + " " + c1y + " "
                 + (p2[0] - (p3[0] - p1[0]) * tension) + " " + c2y + " " + p2[0] + " " + p2[1];
        }
        return d;
    }
    readonly property string linePath: points.length < 2 ? "" : "M " + points[0][0] + " " + points[0][1] + curve
    readonly property string fillPath: points.length < 2 ? "" : "M " + points[0][0] + " " + height
                                                           + " L " + points[0][0] + " " + points[0][1] + curve
                                                           + " L " + points[points.length - 1][0] + " " + height + " Z"

    // Grid line positions along an extent: from its far edge every `cell` pixels, and 0.
    function lines(extent) {
        const out = [];
        let p = extent - 1;
        while (p >= 0) {
            out.push(p);
            p -= cell;
        }
        if (out.length > 0 && out[out.length - 1] > 0)
            out.push(0);
        return out;
    }

    implicitHeight: 64
    clip: true

    Repeater {
        model: root.grid && root.width > 0 ? root.lines(root.width) : []
        Rectangle {
            required property real modelData
            x: modelData
            width: 1
            height: root.height
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
        }
    }

    Repeater {
        model: root.grid && root.height > 0 ? root.lines(root.height) : []
        Rectangle {
            required property real modelData
            y: modelData
            width: root.width
            height: 1
            color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.05)
        }
    }

    Shape {
        anchors.fill: parent
        visible: root.linePath !== ""
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: -1
            fillGradient: LinearGradient {
                x1: 0
                y1: 0
                x2: 0
                y2: root.height
                GradientStop {
                    position: 0
                    color: Qt.rgba(root.color.r, root.color.g, root.color.b, 100 / 255)
                }
                GradientStop {
                    position: 1
                    color: Qt.rgba(root.color.r, root.color.g, root.color.b, 10 / 255)
                }
            }
            PathSvg {
                path: root.fillPath
            }
        }

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.strokeWidth
            fillColor: "transparent"
            joinStyle: ShapePath.BevelJoin
            capStyle: ShapePath.FlatCap
            PathSvg {
                path: root.linePath
            }
        }
    }
}
