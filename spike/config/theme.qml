// The look of the bars and widgets. Every property is optional; the values below are the
// defaults. Edit and save: the bars reload.
import barnacle

ThemeSpec {
    // Colours (translucent by default: the bars blur what is behind them)
    text: "#ffffff"
    surface: "#66101018"       // the bar's background, unless a Bar sets its own `background`
    accent: "#5b8def"          // the focused workspace
    warn: "#e5484d"            // high load, low battery, config errors
    good: "#38c172"            // charging
    caution: "#f0c36d"

    // A widget's background: normal, hovered, shown (active), holding something, empty
    fill: "#22ffffff"
    fillHover: "#44ffffff"
    fillActive: "#55ffffff"
    fillUsed: "#33ffffff"
    fillEmpty: "#18ffffff"

    // Popups and tooltips sit over arbitrary windows, so they are nearly opaque
    popupSurface: "#f0181820"
    popupBorder: "#33ffffff"

    // Type and size in pixels; a negative radius is fully rounded (a pill)
    // fontFamily: "Segoe UI Variable"     // default: the system font
    fontSize: 13
    smallFontSize: 12
    pillHeight: 24
    radius: -1
    padding: 10                // inside a widget, left and right
    spacing: 12                // between widgets in a BarLayout
}
