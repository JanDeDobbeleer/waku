import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import Qt.labs.folderlistmodel

// Never loaded. windeployqt scans this folder (--qmldir) and so deploys the Qt modules that
// shells written for Quickshell import but no built-in wabar file does. See docs/building.md.
Item {}
