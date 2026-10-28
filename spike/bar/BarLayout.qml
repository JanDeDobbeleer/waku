import QtQuick

// The usual content of a Bar: three groups of widgets, start, centre and end.
//
//     layout: BarLayout {
//         start:  [ Clock {}, Workspaces {} ]
//         center: [ Title {} ]
//         end:    [ Volume {}, Battery {} ]
//     }
//
// A widget that sets `visible: false` takes no space. `spacing` (default Theme.spacing) and
// `padding` are in pixels.
Item {
    id: root

    property int spacing: Theme.spacing
    property int padding: 12

    // The widgets are parented straight into the rows, so they behave like ordinary children.
    property alias start: leftRow.data
    property alias center: centerRow.data
    property alias end: rightRow.data

    Row {
        id: leftRow
        anchors.left: parent.left
        anchors.leftMargin: root.padding
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.spacing
    }

    Row {
        id: centerRow
        anchors.centerIn: parent
        spacing: root.spacing
    }

    Row {
        id: rightRow
        anchors.right: parent.right
        anchors.rightMargin: root.padding
        anchors.verticalCenter: parent.verticalCenter
        spacing: root.spacing
    }
}
