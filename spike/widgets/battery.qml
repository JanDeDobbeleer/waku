// Shows only when the machine has a battery.
import QtQuick
import Yasb.Services

Rectangle {
    visible: battery.available
    implicitWidth: visible ? text.implicitWidth + 20 : 0
    implicitHeight: 24
    radius: 12
    color: battery.charging ? "#3338c172" : "#22ffffff"

    Battery { id: battery; interval: 5000 }

    Text {
        id: text
        anchors.centerIn: parent
        color: "white"
        font.pixelSize: 13
        text: (battery.charging ? "⚡ " : "") + battery.percent + "%"
    }
}
