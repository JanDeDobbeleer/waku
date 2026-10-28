import QtQuick

Text {
    id: root

    property string format: "ddd d MMM  HH:mm:ss"

    color: "white"
    font.pixelSize: 14
    text: Qt.formatDateTime(new Date(), format)

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: root.text = Qt.formatDateTime(new Date(), root.format)
    }
}
