// The active GlazeWM binding mode (for example "resize"); a click leaves it. Hidden when there is
// none. GlazeWM-only, so it uses that service directly; the Workspaces widget needs no such thing.
import barnacle.widgets
import barnacle.services

Pill {
    visible: GlazeWM.connected && GlazeWM.bindingMode !== ""
    fill: "#33f0c36d"
    hoverFill: "#55f0c36d"
    textColor: "#f0c36d"
    fontSize: 12
    text: GlazeWM.bindingMode

    onClicked: GlazeWM.disableBindingMode(GlazeWM.bindingMode)
}
