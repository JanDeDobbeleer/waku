import QtQuick
import barnacle
import barnacle.services as S

// Output volume. The wheel changes it, a click opens a panel (slider, mute, output devices), a
// right click opens the sound settings. Hovering shows the current output device. Hidden when
// there is no audio output.
//
//     Volume { step: 5; format: "🔊 %1%"; popup: false }
//
// `format`: %1 is the volume in percent. `mutedFormat` is shown while muted. With `popup: false`
// a click mutes instead of opening the panel.
Pill {
    id: root

    property int step: 5
    property string format: "🔊 %1%"
    property string mutedFormat: "🔇 muted"
    property bool popup: true

    function deviceName() {
        for (const device of S.AudioOutput.devices)
            if (device.id === S.AudioOutput.defaultDevice)
                return device.name;
        return "";
    }

    visible: S.AudioOutput.available
    text: S.AudioOutput.muted ? mutedFormat : format.replace("%1", S.AudioOutput.volume)
    tooltip: deviceName() !== "" ? deviceName() : "Output volume"

    onClicked: popup ? panel.toggle() : S.AudioOutput.muted = !S.AudioOutput.muted
    onRightClicked: S.Shell.open("ms-settings:sound")
    onScrolled: delta => S.AudioOutput.volume += delta > 0 ? step : -step

    PopupWindow {
        id: panel
        objectName: "volumePopup"
        anchorItem: root
        spacing: 8

        Text {
            color: Theme.text
            opacity: 0.6
            font.family: Theme.fontFamily
            font.pixelSize: Theme.smallFontSize
            text: "Output"
        }

        Row {
            spacing: 10

            Slider {
                objectName: "volumeSlider"
                width: 210
                value: S.AudioOutput.volume
                onMoved: v => S.AudioOutput.volume = Math.round(v)
            }

            Text {
                width: 36
                anchors.verticalCenter: parent.verticalCenter
                horizontalAlignment: Text.AlignRight
                color: Theme.text
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSize
                text: S.AudioOutput.volume + "%"
            }
        }

        Pill {
            text: S.AudioOutput.muted ? "Unmute" : "Mute"
            onClicked: S.AudioOutput.muted = !S.AudioOutput.muted
        }

        Repeater {
            model: S.AudioOutput.devices

            delegate: Pill {
                required property var modelData

                width: 256
                maxTextWidth: 232
                text: modelData.name
                fill: modelData.id === S.AudioOutput.defaultDevice ? Theme.accentFill : Theme.fill
                onClicked: S.AudioOutput.setDefaultDevice(modelData.id)
            }
        }
    }
}
