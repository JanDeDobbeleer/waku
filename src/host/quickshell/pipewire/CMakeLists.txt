# The QML module `Quickshell.Services.Pipewire`: Pipewire, PwNode and the trackers over the audio
# services (default output and input, the per-app sessions).
wabar_quickshell_qml_module(wabar_quickshell_pipewire Quickshell.Services.Pipewire
  QML_FILES
    PwLink.qml
    PwLinkGroup.qml
    PwNode.qml
    PwNodeAudio.qml
    PwNodeLinkTracker.qml
    PwObjectTracker.qml
  SINGLETONS
    Pipewire.qml
  DEPENDENCIES wabar wabar.services)
