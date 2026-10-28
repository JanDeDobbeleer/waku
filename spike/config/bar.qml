// The sample barnacle config. Point the spike at another folder with --config-home <dir>.
// Edit and save: the bars reload. A mistake keeps the previous bars and shows an error pill.
import barnacle
import "widgets"

Config {
    Bar {
        screens: ["primary"]          // "primary", "all", or a screen name such as "DISPLAY2"
        position: Bar.Top
        height: 40
        backdrop: Bar.Blur            // Bar.None | Bar.Blur | Bar.Mica

        layout: BarLayout {           // built once per matching screen
            start: [
                Clock {}
            ]
            center: [
                WorkspacePills {},
                BindingModePill {}
            ]
            end: [
                UserPill {},
                MediaPill {},
                VolumePill {},
                BatteryPill {},
                RamPill {}
            ]
        }
    }
}
