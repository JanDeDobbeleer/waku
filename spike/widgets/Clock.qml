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

    // ISO 8601 week number of a day, as Qt's QDate::weekNumber.
    function isoWeek(day) {
        const d = new Date(Date.UTC(day.getFullYear(), day.getMonth(), day.getDate()));
        const weekday = d.getUTCDay() || 7;
        d.setUTCDate(d.getUTCDate() + 4 - weekday);
        const first = new Date(Date.UTC(d.getUTCFullYear(), 0, 1));
        return Math.ceil(((d - first) / 86400000 + 1) / 7);
    }

    // The calendar popup of Python YASB: the selected day on the left (weekday, month, year, the
    // day number big and the week), the month grid on the right.
    PopupWindow {
        id: popup
        objectName: "calendarPopup"
        anchorItem: root
        padding: 16
        onOpened: month.reset()

        Row {
            spacing: 6

            Column {
                id: summary
                width: 180
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    width: parent.width
                    topPadding: 8
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    font.bold: true
                    text: Qt.locale().dayName(month.selected.getDay(), Locale.LongFormat)
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    text: Qt.locale().monthName(month.month)
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.text
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    text: month.year
                }

                Text {
                    width: parent.width
                    topPadding: -12
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.accent
                    font.family: Theme.fontFamily
                    font.pixelSize: 88
                    font.weight: Font.Black
                    text: month.selected.getDate()
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    color: Qt.alpha(Theme.text, 0.6)
                    font.family: Theme.fontFamily
                    font.pixelSize: 12
                    text: "Week " + root.isoWeek(month.selected)
                }
            }

            Calendar {
                id: month
                objectName: "calendar"
            }
        }
    }

    Component.onCompleted: refresh()
}
