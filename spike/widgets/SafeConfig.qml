// The bar of safe mode (`--safe`): what starts when the config is broken or in the way. It reads
// no file from the config home and uses the default theme. Not meant to be copied; start from
// the sample config instead.
import tidy

Config {
    Bar {
        layout: BarLayout {
            start: [
                Clock {}
            ]
            center: [
                Workspaces {}
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
