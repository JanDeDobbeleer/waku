import QtQuick

// Quickshell.Services.Pipewire's PwNodeAudio over a waku audio object (the default output, the
// default input or one app's audio session). `volume` is 0 to 1 (above 1 is clamped on the way
// out); writing `volume` or `muted` calls the service's own setter, and a change of the service
// updates both.
//
// `source` is the audio object: `volume`, `muted` and a `changed` signal. `scale` converts the
// source's volume to 0..1 (0.01 for the endpoints, which count percent; 1 for sessions).
QtObject {
    id: root

    final property QtObject source: null
    final property real scale: 1

    // Per channel; waku has one volume per node, so every channel reports it.
    final readonly property var channels: []
    final readonly property var volumes: [volume]
    final property real volume: 0
    final property bool muted: false

    // True while the source is copied in, so the handlers below do not write it back.
    final property bool pulling: false

    function pull() {
        if (!source)
            return;
        pulling = true;
        volume = source.volume * scale;
        muted = source.muted;
        pulling = false;
    }

    onSourceChanged: pull()

    onVolumeChanged: {
        if (pulling || !source)
            return;
        const wanted = Math.max(0, Math.min(1, volume)) / scale;
        source.volume = scale === 1 ? wanted : Math.round(wanted);
    }

    onMutedChanged: {
        if (!pulling && source)
            source.muted = muted;
    }

    final readonly property Connections watch: Connections {
        target: root.source
        ignoreUnknownSignals: true
        function onChanged() {
            root.pull();
        }
    }
}
