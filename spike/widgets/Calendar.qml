import QtQuick
import tidy

// A month grid for popups, drawn like the calendar of Python YASB: a row of weekday names and six
// rows of days, the selected day (today at first) as an accent filled pill. The wheel (or, with
// `navigation`, the arrows) changes the month; the week starts on the system locale's first day.
//
//     Calendar { onDayClicked: day => console.log(day) }
//
// `selected` is the day shown in the pill; a click selects a day and `dayClicked` reports it. A new
// month keeps the selected day number (clamped to the month's length). `reset()` returns to
// today's month.
Item {
    id: root

    property int year: new Date().getFullYear()
    // 0 to 11, as in JavaScript's Date.
    property int month: new Date().getMonth()
    property int cellSize: 36
    property date selected: new Date()
    // Show the arrows and the month name above the grid. Python YASB hides them; the wheel steps.
    property bool navigation: false

    signal dayClicked(date day)

    readonly property int firstDay: Qt.locale().firstDayOfWeek % 7
    // The first cell: the first of the month, moved back to the start of its week.
    readonly property date firstShown: {
        const first = new Date(year, month, 1);
        return new Date(year, month, 1 - (first.getDay() - firstDay + 7) % 7);
    }

    function reset() {
        const now = new Date();
        year = now.getFullYear();
        month = now.getMonth();
        selected = now;
    }

    function step(delta) {
        const moved = new Date(year, month + delta, 1);
        year = moved.getFullYear();
        month = moved.getMonth();
        const length = new Date(year, month + 1, 0).getDate();
        selected = new Date(year, month, Math.min(selected.getDate(), length));
    }

    implicitWidth: 7 * cellSize
    implicitHeight: header.height + weekdays.height + grid.height + 4

    Row {
        id: header
        visible: root.navigation
        width: parent.width
        height: visible ? root.cellSize : 0

        Text {
            width: root.cellSize
            height: parent.height
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: "‹"
            TapHandler {
                onTapped: root.step(-1)
            }
        }

        Text {
            width: parent.width - 2 * root.cellSize
            height: parent.height
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            font.bold: true
            text: Qt.locale().monthName(root.month) + " " + root.year
        }

        Text {
            width: root.cellSize
            height: parent.height
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSize
            text: "›"
            TapHandler {
                onTapped: root.step(1)
            }
        }
    }

    Row {
        id: weekdays
        anchors.top: header.bottom
        height: root.cellSize * 0.8

        Repeater {
            model: 7
            delegate: Text {
                required property int index

                width: root.cellSize
                height: weekdays.height
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Qt.alpha(Theme.text, 0.9)
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: Qt.locale().dayName((root.firstDay + index) % 7, Locale.ShortFormat)
            }
        }
    }

    Grid {
        id: grid
        anchors.top: weekdays.bottom
        anchors.topMargin: 4
        columns: 7

        Repeater {
            model: 42
            delegate: Item {
                id: cell

                required property int index
                readonly property date day: new Date(root.firstShown.getFullYear(), root.firstShown.getMonth(),
                                                     root.firstShown.getDate() + index)
                readonly property bool inMonth: day.getMonth() === root.month
                readonly property bool isSelected: day.toDateString() === root.selected.toDateString()

                width: root.cellSize
                height: Math.round(root.cellSize * 0.8)

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: Math.min(12, height / 2)
                    color: cell.isSelected ? Theme.accent : (hover.hovered ? Theme.fillHover : "transparent")
                }

                Text {
                    anchors.centerIn: parent
                    color: cell.isSelected ? Theme.onAccent : Qt.alpha(Theme.text, 0.9)
                    opacity: cell.inMonth ? 1 : 0.4
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSize
                    font.bold: cell.isSelected
                    text: cell.day.getDate()
                }

                HoverHandler {
                    id: hover
                }

                TapHandler {
                    onTapped: {
                        root.year = cell.day.getFullYear();
                        root.month = cell.day.getMonth();
                        root.selected = cell.day;
                        root.dayClicked(cell.day);
                    }
                }
            }
        }
    }

    WheelHandler {
        onWheel: event => root.step(event.angleDelta.y > 0 ? -1 : 1)
    }
}
