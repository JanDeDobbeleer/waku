// A custom widget: any command's output on a Pill. No C++, no registration; the file name is the
// type name, so shell.qml uses it as `Whoami {}` after `import "widgets"`.
import wabar.widgets
import wabar.services

Pill {
    interactive: false
    text: whoami.output

    ShellProcess {
        id: whoami
        program: "whoami"
        interval: 60000
    }
}
