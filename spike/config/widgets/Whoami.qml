// A custom widget: any command's output on a Pill. No C++, no registration; the file name is the
// type name, so bar.qml uses it as `Whoami {}` after `import "widgets"`.
import tidy.widgets
import tidy.services

Pill {
    interactive: false
    text: whoami.output

    Process {
        id: whoami
        program: "whoami"
        interval: 60000
    }
}
