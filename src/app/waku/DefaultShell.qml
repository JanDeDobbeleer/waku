// The built-in bar: waku shows it when the config home has no shell.qml. Copy examples/config to
// your config home (%USERPROFILE%\.config\waku) to customise; once a shell.qml is there, it
// replaces this bar.
import waku

ShellRoot {
    Panel {
        thickness: 40
        backdrop: Backdrop.Blur
        content: PanelLayout {
            start: [
                Clock {}
            ]
            center: [
                // qmllint disable required
                Workspaces {}
                // qmllint enable required
            ]
            end: [
                Volume {},
                Battery {},
                Cpu {},
                Memory {}
            ]
        }
    }
}
