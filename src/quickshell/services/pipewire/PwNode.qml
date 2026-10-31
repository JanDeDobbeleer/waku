import QtQuick

// Quickshell.Services.Pipewire's PwNode over a wabar audio object. Pipewire creates these; a shell
// does not declare one.
//
//   kind  "sink" (the default output), "source" (the default input) or "stream" (one app's audio
//         session)
//
// `source` is the wabar object behind the node: AudioOutput, AudioInput or an AudioStream.
// `properties` holds the keys shells read: application.name, application.icon-name, media.class,
// node.name and node.description. There is no media.title or media.artist.
QtObject {
    id: root

    property QtObject source: null
    property string kind: "stream"

    readonly property bool isSink: kind === "sink"
    readonly property bool isStream: kind === "stream"
    readonly property bool ready: source !== null
    // Quickshell's PwNode also has an integer `id` (the Pipewire object id); QML cannot declare a
    // property named id, so it is missing. `pid` is the process id of a stream, else 0.
    readonly property int pid: kind === "stream" && source && source.pid !== undefined ? source.pid : 0
    readonly property string name: kind === "stream" ? (source ? String(source.name) : "") : deviceName
    readonly property string description: kind === "stream" ? (source ? String(source.description) : "") : deviceName
    readonly property string nickname: description
    readonly property string type: kind === "sink" ? "Audio/Sink" : (kind === "source" ? "Audio/Source" : "Stream/Output/Audio")
    readonly property var properties: {
        const base = ({
                "media.class": type,
                "node.name": name,
                "node.description": description
            });
        if (kind !== "stream" || !source || !source.properties)
            return base;
        const extra = source.properties;
        for (const key in extra)
            base[key] = extra[key];
        base["application.icon-name"] = extra["application.icon_name"] !== undefined ? extra["application.icon_name"] : "";
        return base;
    }

    // The name of the endpoint that is the default now.
    property string deviceName: {
        if (!source || kind === "stream")
            return "";
        const list = source.devices;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === source.defaultDevice)
                return list[i].name;
        }
        return kind === "sink" ? "Default output" : "Default input";
    }

    readonly property PwNodeAudio audio: PwNodeAudio {
        source: root.source
        scale: root.kind === "stream" ? 1 : 0.01
    }
}
