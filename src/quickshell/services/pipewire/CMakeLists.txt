# The QML module `Quickshell.Services.Pipewire`: Pipewire, PwNode and the trackers over the audio
# services (default output and input, the per-app sessions).
waku_quickshell_qml_module(waku_quickshell_pipewire Quickshell.Services.Pipewire
  QML_FILES
    PwLink.qml
    PwLinkGroup.qml
    PwNode.qml
    PwNodeAudio.qml
    PwNodeLinkTracker.qml
    PwObjectTracker.qml
  SINGLETONS
    Pipewire.qml
  DEPENDENCIES waku waku.services)
