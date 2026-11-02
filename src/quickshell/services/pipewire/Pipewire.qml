pragma Singleton
import QtQuick
import waku as W
import waku.services as S

// Quickshell.Services.Pipewire's `Pipewire` singleton over waku's audio services.
//
//   defaultAudioSink / defaultAudioSource  the default output and input as PwNode; null while
//                                          the service has no endpoint
//   nodes          a model (with `values`) of those two nodes and one node per app audio session
//                  (the sessions of the default output; system sounds are left out)
//   ready          true
//   preferredDefaultAudioSink / Source  a node set here makes its device the system default (a
//                  node of the session kind is ignored)
//   links, linkGroups  always empty: Windows has no routing graph
//
// Every node is bound, so PwObjectTracker does nothing. `output`, `input` and `sessions` are the
// objects the nodes read (AudioOutput, AudioInput, the AudioStreams model); tests replace them.
QtObject {
    id: root

    property QtObject output: S.AudioOutput
    property QtObject input: S.AudioInput
    property var sessions: S.AudioStreams.streams

    readonly property bool ready: true
    readonly property PwNode defaultAudioSink: output && output.available ? sinkNode : null
    readonly property PwNode defaultAudioSource: input && input.available ? sourceNode : null

    property QtObject preferredDefaultAudioSink: null
    property QtObject preferredDefaultAudioSource: null

    readonly property W.ScriptModel links: W.ScriptModel {}
    readonly property W.ScriptModel linkGroups: W.ScriptModel {}
    readonly property W.ScriptModel nodes: W.ScriptModel {
        values: {
            const list = [];
            if (root.defaultAudioSink)
                list.push(root.defaultAudioSink);
            if (root.defaultAudioSource)
                list.push(root.defaultAudioSource);
            return list.concat(root.sessionNodes);
        }
    }

    property var sessionNodes: []

    readonly property PwNode sinkNode: PwNode {
        kind: "sink"
        source: root.output
    }
    readonly property PwNode sourceNode: PwNode {
        kind: "source"
        source: root.input
    }

    // Sets the default device of the endpoint behind `node`, found by its name.
    function prefer(node, endpoint) {
        if (!node || node.isStream || !endpoint)
            return;
        const list = endpoint.devices;
        for (let i = 0; i < list.length; ++i) {
            if (list[i].name === node.name) {
                endpoint.setDefaultDevice(list[i].id);
                return;
            }
        }
    }

    onPreferredDefaultAudioSinkChanged: prefer(preferredDefaultAudioSink, output)
    onPreferredDefaultAudioSourceChanged: prefer(preferredDefaultAudioSource, input)

    readonly property Instantiator sessionNodeFactory: Instantiator {
        model: root.sessions
        delegate: PwNode {
            required property var modelData
            kind: "stream"
            source: modelData
        }
        onObjectAdded: (index, object) => {
            const list = root.sessionNodes.slice();
            list.splice(index, 0, object);
            root.sessionNodes = list;
        }
        onObjectRemoved: (index, object) => {
            const list = root.sessionNodes.slice();
            list.splice(index, 1);
            root.sessionNodes = list;
        }
    }
}
