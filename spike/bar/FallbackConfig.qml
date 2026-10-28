import QtQuick

// Used when the very first load of bar.qml fails, so the desktop still gets a bar that shows the
// error (the error pill of BarWindow) instead of nothing.
Config {
    Bar {
        layout: BarLayout {
            start: [
                Text {
                    color: "white"
                    font.pixelSize: 13
                    text: "barnacle"
                }
            ]
        }
    }
}
