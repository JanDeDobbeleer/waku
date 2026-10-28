import QtQuick
import tidy

// A month grid for popups. The wheel (or the arrows) changes the month; the week starts on the
// system locale's first day.
//
//     Calendar { onDayClicked: day => console.log(day) }
//
// `reset()` returns to the current month.
Item {
    id: root

    property int year: new Date().getFullYear()
    // 0 to 11, as in JavaScript's Date.
    property int month: new Date().getMonth()
    property int cellSize: 30

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
    }

    function step(delta) {
        const moved = new Date(year, month + delta, 1);
        year = moved.getFullYear();
        month = moved.getMonth();
    }

    implicitWidth: 7 * cellSize
    implicitHeight: header.height + weekdays.height + grid.height + 8

    Row {
        id: header
        width: parent.width
        height: root.cellSize

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
                color: Theme.text
                opacity: 0.55
                font.family: Theme.fontFamily
                font.pixelSize: Theme.smallFontSize
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
            delegate: Rectangle {
                id: cell

                required property int index
                readonly property date day: new Date(root.firstShown.getFullYear(), root.firstShown.getMonth(),
                                                     root.firstShown.getDate() + index)
                readonly property bool inMonth: day.getMonth() === root.month
                readonly property bool isToday: day.toDateString() === new Date().toDateString()

                width: root.cellSize
                height: root.cellSize * 0.9
                radius: Theme.radius >= 0 ? Math.min(Theme.radius, height / 2) : height / 2
                color: isToday ? Theme.accent : (hover.hovered ? Theme.fillHover : "transparent")

                Text {
                    anchors.centerIn: parent
                    color: Theme.text
                    opacity: cell.inMonth ? 1 : 0.35
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.smallFontSize
                    text: cell.day.getDate()
                }

                HoverHandler {
                    id: hover
                }

                TapHandler {
                    onTapped: root.dayClicked(cell.day)
                }
            }
        }
    }

    WheelHandler {
        onWheel: event => root.step(event.angleDelta.y > 0 ? -1 : 1)
    }
}
