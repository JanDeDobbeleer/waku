// The active GlazeWM binding mode (for example "resize"). Hidden when there is none. This is
// GlazeWM-only, so it uses the GlazeWM backend directly; the workspaces come from Workspaces.
import QtQuick
import barnacle.services

Rectangle {
    visible: GlazeWM.connected && GlazeWM.bindingMode !== ""
    implicitWidth: label.implicitWidth + 20
    implicitHeight: 24
    radius: 12
    color: "#33f0c36d"

    Text {
        id: label
        anchors.centerIn: parent
        color: "#f0c36d"
        font.pixelSize: 12
        text: GlazeWM.bindingMode
    }

    TapHandler { onTapped: GlazeWM.disableBindingMode(GlazeWM.bindingMode) }
}
