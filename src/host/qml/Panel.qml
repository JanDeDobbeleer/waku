import QtQuick

// A panel docked to a screen edge, one PanelWindow per matching screen: the short form for a
// hand-written shell.
//
//     ShellRoot {
//         Panel {
//             screens: ["all"]
//             edge: Panel.Top
//             thickness: 40
//             backdrop: Backdrop.Blur
//             content: PanelLayout { start: [ Clock {} ] }
//         }
//     }
//
//   screens     "primary", "all" or screen names ("DISPLAY2"); one window each
//   edge        Panel.Top | Bottom | Left | Right (an app bar on each; Left and Right are vertical)
//   thickness   the height of a top or bottom panel, the width of a side one
//   exclusive   reserve the work area (takes effect with --register-app-bar)
//   backdrop    Backdrop.None | Blur | Mica
//   background  the surface colour; unset: Theme.surface
//   enabled     false builds no window
//   content     built once per window; a failing one leaves that window out
//
// The same panel in Quickshell's words is a Variants over ScreenList.screens of PanelWindows.
Variants {
    id: panel

    enum Edge {
        Top,
        Bottom,
        Left,
        Right
    }

    property var screens: ["primary"]
    property int edge: Panel.Top
    property int thickness: 40
    property bool exclusive: false
    property int backdrop: Backdrop.None
    property color background
    property bool enabled: true
    property Component content: null

    model: {
        ScreenList.screens;  // rebuild when the screens change
        return panel.enabled ? ScreenList.match(panel.screens) : [];
    }

    delegate: PanelWindow {
        required property ShellScreen modelData

        screen: modelData
        anchors.top: panel.edge !== Panel.Bottom
        anchors.bottom: panel.edge !== Panel.Top
        anchors.left: panel.edge !== Panel.Right
        anchors.right: panel.edge !== Panel.Left
        implicitWidth: panel.thickness
        implicitHeight: panel.thickness
        exclusionMode: panel.exclusive ? ExclusionMode.Normal : ExclusionMode.Ignore
        exclusiveZone: panel.exclusive ? panel.thickness : 0
        backdrop: panel.backdrop
        background: panel.background
        content: panel.content
    }
}
