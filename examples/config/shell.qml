// The sample wabar shell. Point wabar at another folder with --config-home <dir>.
// Edit and save: the shell reloads. A mistake keeps the previous shell and shows an error pill.
import wabar
import wabar.widgets          // the built-in widgets
import "widgets"           // your own, one component per file (see widgets/)

ShellRoot {
    Panel {
        screens: ["primary"]          // "primary", "all", or a screen name such as "DISPLAY2"
        edge: Panel.Top               // Panel.Top | Panel.Bottom
        thickness: 40
        backdrop: Backdrop.Blur       // Backdrop.None | Backdrop.Blur | Backdrop.Mica

        content: PanelLayout {        // built once per matching screen
            start: [
                Clock {}
            ]
            center: [
                Workspaces {},
                BindingMode {}
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
