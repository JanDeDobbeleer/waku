import QtQuick
import tidy

// The date and time. A click opens a calendar; hovering shows the full date.
//
//     Clock { format: "HH:mm"; calendar: false }
//
// `format` is a Qt date-time format (https://doc.qt.io/qt-6/qml-qtqml-qt.html#formatDateTime-method).
// It redraws once a minute, or once a second when the format shows seconds. `tooltipFormat` is the
// format of the hover text. `calendar: false` drops the popup and the hover highlight.
Pill {
    id: root

    property string format: "ddd d MMM  HH:mm"
    property string tooltipFormat: "dddd, d MMMM yyyy"
    property bool calendar: true
    property date now: new Date()
    readonly property bool showsSeconds: format.indexOf("s") >= 0

    interactive: calendar
    text: Qt.formatDateTime(now, format)
    tooltip: Qt.formatDateTime(now, tooltipFormat)

    onClicked: if (calendar)
        popup.toggle()

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

    PopupWindow {
        id: popup
        objectName: "calendarPopup"
        anchorItem: root
        onOpened: month.reset()

        Calendar {
            id: month
            objectName: "calendar"
        }
    }

    Component.onCompleted: refresh()
}
