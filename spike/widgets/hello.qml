// A user widget: no C++, no registration. Edit and save; the bar reloads it.
import QtQuick
import Yasb.Services

Rectangle {
    implicitWidth: row.implicitWidth + 20
    implicitHeight: 24
    radius: 12
    color: "#335b8def"

    Cpu { id: cpu; interval: 1000 }

    // Any command's output becomes a property; rerun every 5 s.
    Process {
        id: user
        program: "whoami"
        interval: 5000
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 8
        Text { text: user.output; color: "white"; font.pixelSize: 13 }
        Text { text: "CPU " + Math.round(cpu.percent) + "%"; color: "#cde"; font.pixelSize: 13 }
    }
}
