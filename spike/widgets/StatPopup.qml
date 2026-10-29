import QtQuick
import wabar

// The popup of the CPU and memory widgets, as in Python YASB: a title with a pin, a graph of the
// recent history and a grid of stat cards.
//
//     StatPopup {
//         anchorItem: pill
//         boldTitle: "CPU"
//         title: "Usage"
//         history: cpu.history
//         stats: [{ label: "Usage", value: "13%" }, { label: "Frequency", value: "1622 MHz" }]
//     }
//
// `stats` fills the cards two per row, the first of each row on the left. `graph` and `graphGrid`
// switch the chart and its grid. The pin only marks the popup (it still closes when it loses focus).
PopupWindow {
    id: root

    property string boldTitle: ""
    property string title: ""
    property var history: []
    property var stats: []
    property color graphColor: Theme.accent
    property bool graph: true
    property bool graphGrid: true
    property bool pinned: false
    readonly property color muted: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.6)

    padding: 0
    spacing: 0

    Item {
        width: 400
        height: 46

        Text {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.text
            font.family: Theme.fontFamily
            font.pixelSize: 16
            textFormat: Text.RichText
            text: "<b>" + root.boldTitle + "</b> " + root.title
        }

        Text {
            objectName: "pinButton"
            x: parent.width - width - 16
            anchors.verticalCenter: parent.verticalCenter
            color: pinArea.containsMouse || root.pinned ? Theme.text : root.muted
            font.family: Theme.iconFont
            font.pixelSize: 14
            text: root.pinned ? "" : ""

            MouseArea {
                id: pinArea
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                onClicked: root.pinned = !root.pinned
            }
        }
    }

    Text {
        visible: root.graph
        leftPadding: 14
        bottomPadding: 4
        color: root.muted
        font.family: Theme.fontFamily
        font.pixelSize: 12
        text: "Utilization"
    }

    StatGraph {
        objectName: "statGraph"
        visible: root.graph
        width: 400
        height: 64
        values: root.history
        color: root.graphColor
        grid: root.graphGrid
    }

    Item {
        width: 400
        height: grid.implicitHeight + 48

        Grid {
            id: grid
            x: 24
            y: 24
            columns: 2
            columnSpacing: 16
            rowSpacing: 16

            Repeater {
                model: root.stats

                Rectangle {
                    required property int index
                    required property var modelData
                    width: 168
                    height: 82
                    radius: 8
                    color: Theme.fillEmpty
                    border.color: Theme.fill
                    border.width: 1

                    Column {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 16
                        anchors.topMargin: 8

                        Text {
                            width: parent.width
                            topPadding: 6
                            bottomPadding: 2
                            horizontalAlignment: index % 2 ? Text.AlignRight : Text.AlignLeft
                            color: root.muted
                            font.family: Theme.fontFamily
                            font.pixelSize: 13
                            text: modelData.label
                        }

                        Text {
                            objectName: "statValue" + index
                            width: parent.width
                            horizontalAlignment: index % 2 ? Text.AlignRight : Text.AlignLeft
                            color: Theme.text
                            font.family: Theme.fontFamily
                            font.pixelSize: 20
                            font.weight: Font.Bold
                            text: modelData.value
                        }
                    }
                }
            }
        }
    }
}
