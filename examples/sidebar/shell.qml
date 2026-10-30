// A vertical sidebar: one PanelWindow docked to the left edge of every screen. Run it with
//
//     wabar --config-home examples/sidebar
//
// The panel is anchored to the left edge and stretched between the top and the bottom, so wabar
// docks it as a left app bar (`QsWindow.window.dockEdge` is DockEdge.Left). PanelLayout runs its
// groups top to bottom: `start` at the top, `center` in the middle, `end` at the bottom. Pills show
// the icon above the text; popups and tooltips open to the right of the bar.
//
// The panel reserves its width in the work area only when wabar runs with --register-app-bar;
// without it the bar floats over the desktop. For a right sidebar swap `left` for `right`.
import QtQuick
import wabar
import wabar.widgets

ShellRoot {
    Variants {
        model: ScreenList.screens

        PanelWindow {
            required property ShellScreen modelData

            screen: modelData
            anchors {
                left: true
                top: true
                bottom: true
            }
            implicitWidth: 56
            exclusionMode: ExclusionMode.Normal
            exclusiveZone: 56
            backdrop: Backdrop.Blur

            PanelLayout {
                anchors.fill: parent
                startPadding: 8
                endPadding: 8
                spacing: 8

                start: [
                    Clock {
                        objectName: "clock"
                        format: "HH:mm"
                    }
                ]
                center: [
                    Workspaces {
                        objectName: "workspaces"
                    }
                ]
                end: [
                    Volume {
                        objectName: "volume"
                    },
                    Battery {
                        objectName: "battery"
                    },
                    Tray {
                        objectName: "tray"
                        unpinnedInPopup: true
                    }
                ]
            }
        }
    }
}
