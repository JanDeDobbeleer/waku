import QtQuick
import barnacle.services as S

// Output volume. The wheel changes it, a click mutes, a right click opens the sound settings.
// Hidden when there is no audio output.
//
//     Volume { step: 5; format: "🔊 %1%" }
//
// `format`: %1 is the volume in percent. `mutedFormat` is shown while muted.
Pill {
    id: root

    property int step: 5
    property string format: "🔊 %1%"
    property string mutedFormat: "🔇 muted"

    visible: S.AudioOutput.available
    text: S.AudioOutput.muted ? mutedFormat : format.replace("%1", S.AudioOutput.volume)

    onClicked: S.AudioOutput.muted = !S.AudioOutput.muted
    onRightClicked: S.Shell.open("ms-settings:sound")
    onScrolled: delta => S.AudioOutput.volume += delta > 0 ? step : -step
}
