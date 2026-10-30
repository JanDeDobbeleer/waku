import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Templates as T
import Qt.labs.folderlistmodel

// Never loaded. windeployqt scans this folder (--qmldir) and so deploys the Qt modules that
// shells written for Quickshell import but no built-in wabar file does. QtQuick.Controls.Basic
// (the one Controls style; no native style is deployed) pulls in QtQuick.Controls and its impl
// modules. See docs/building.md.
Item {}
