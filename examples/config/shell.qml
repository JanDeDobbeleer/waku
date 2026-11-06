// The sample waku shell. Point waku at another folder with --config-home <dir>.
// Edit and save: the shell reloads. A mistake keeps the previous shell and shows an error pill.
import waku
import waku.widgets          // the built-in widgets
import "widgets"           // your own, one component per file (see widgets/)

ShellRoot {
    Panel {
        screens: ["primary"]          // "primary", "all", or a screen name such as "DISPLAY2"
        edge: Panel.Top               // Panel.Top | Panel.Bottom
        thickness: 40
        // Blur is the DWM acrylic blur of what is behind the bar. The compositor draws it, so its
        // cost lands on dwm.exe and the GPU, not on waku; None and Mica skip it. Measured on one
        // machine: waku's own CPU use was the same for all three, and dwm's share was not visible.
        backdrop: Backdrop.Blur       // Backdrop.None | Backdrop.Blur | Backdrop.Mica

        content: PanelLayout {        // built once per matching screen
            start: [
                Clock {}
            ]
            center: [
                Workspaces {}
            ]
            end: [
                Whoami {},
                Media {},
                Volume {},
                // Tray {},                   // notification-area icons (starts the tray hook)
                Battery {},
                Cpu {},
                Memory {}
            ]
        }
    }

    // The same bar in Quickshell's words, one PanelWindow per screen:
    //
    //     Variants {
    //         model: ScreenList.screens
    //         PanelWindow {
    //             required property ShellScreen modelData
    //             screen: modelData
    //             anchors { top: true; left: true; right: true }
    //             implicitHeight: 40
    //             backdrop: Backdrop.Blur
    //             PanelLayout { anchors.fill: parent; start: [ Clock {} ] }
    //         }
    //     }
}
