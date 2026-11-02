// The built-in bar: waku shows it when the config home has no shell.qml. `wakuc config init`
// writes this file as your shell.qml (the comment block above `import` is replaced there); once a
// shell.qml exists, it replaces this bar.
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
