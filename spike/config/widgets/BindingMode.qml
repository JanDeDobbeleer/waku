// The active GlazeWM binding mode (for example "resize"); a click leaves it. Hidden when there is
// none. GlazeWM-only, so it uses that service directly; the Workspaces widget needs no such thing.
import tidy
import tidy.widgets
import tidy.services

Pill {
    visible: GlazeWM.connected && GlazeWM.bindingMode !== ""
    fill: Theme.cautionFill
    hoverFill: Qt.alpha(Theme.caution, 0.4)
    textColor: Theme.caution
    fontSize: Theme.smallFontSize
    text: GlazeWM.bindingMode

    onClicked: GlazeWM.disableBindingMode(GlazeWM.bindingMode)
}
