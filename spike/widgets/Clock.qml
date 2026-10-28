import QtQuick

// The date and time.
//
//     Clock { format: "HH:mm" }
//
// `format` is a Qt date-time format (https://doc.qt.io/qt-6/qml-qtqml-qt.html#formatDateTime-method).
// It redraws once a minute, or once a second when the format shows seconds.
Pill {
    id: root

    property string format: "ddd d MMM  HH:mm"
    property date now: new Date()
    readonly property bool showsSeconds: format.indexOf("s") >= 0

    interactive: false
    text: Qt.formatDateTime(now, format)

    function nextTick() {
        const d = new Date();
        const elapsed = showsSeconds ? d.getMilliseconds() : d.getSeconds() * 1000 + d.getMilliseconds();
        // A few ms past the boundary, so the redraw never lands just before it.
        return (showsSeconds ? 1000 : 60000) - elapsed + 20;
    }

    function refresh() {
        now = new Date();
        tick.interval = nextTick();
        tick.restart();
    }

    Timer {
        id: tick
        repeat: false
        onTriggered: root.refresh()
    }

    Component.onCompleted: refresh()
}
