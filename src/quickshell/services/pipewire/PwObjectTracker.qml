import QtQuick

// Quickshell.Services.Pipewire's PwObjectTracker. Pipewire objects are bound here from the start,
// so tracking changes nothing; the type exists so shells that declare it load.
QtObject {
    final property list<QtObject> objects
}
