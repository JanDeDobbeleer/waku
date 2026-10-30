#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace wabar::host {

// Guards the QML plugins a user builds into `<config home>/modules` or an `--import-path` folder.
// A plugin built against another Qt version, in the other debug/release flavour, or for another
// processor architecture fails to load, or worse; the guard reads the plugin's embedded metadata
// and its PE header from the file, without loading it, and refuses a plugin that does not fit.
//
// The unit is a whole import root: a root is handed to the QML engine only when every plugin
// under it fits (`usableImportRoots`). The shell host validates a config in a second engine that
// copies the import paths, so a root that is not added is not seen by either.
//
// Threading: any thread; no state.

struct PluginVerdict {
  bool ok = true;
  QString reason;  // one line, set when !ok
};

// What a plugin's metadata (QPluginLoader::metaData(), the `QtPluginMetaData` of the file) must
// say to load in this process: `version` is QT_VERSION of the Qt that built it, equal in major
// and minor to `qtVersion`; `debug` equals `debugBuild`. Missing keys refuse.
PluginVerdict checkPluginMetadata(const QJsonObject& metaData, int qtVersion, bool debugBuild);

// The processor of a Windows binary: IMAGE_FILE_MACHINE_* of its PE header, 0 when `bytes` is not
// a PE file. `hostMachine()` is the same value for this process.
quint16 peMachine(const QByteArray& header);
quint16 hostMachine();

// A plugin file against this process: readable, a PE file for this processor, with metadata that
// fits (checkPluginMetadata with QT_VERSION and this build's flavour).
PluginVerdict checkPluginFile(const QString& libraryPath);

// The library files named by the `qmldir` files under `root` (`plugin Name [dir]` lines). A name
// resolves to `Name.dll`, or `Named.dll` in a debug build; a plugin whose file is missing is
// left out (the engine reports a missing plugin itself).
QStringList findPluginFiles(const QString& root);

// A plugin that failed its check, with the reason.
struct PluginFailure {
  QString file;
  QString reason;
};

// Every plugin named by a `qmldir` anywhere under `configHome` (a `qmldir` beside shell.qml, under
// a folder reached by `import "./lib"`, ...) that fails checkPluginFile. Folders in `skipRoots`
// (the import roots already checked) are not entered; symbolic links, `.git` and `node_modules`
// are not followed, and the walk stops at depth 8, so the scan stays cheap.
QList<PluginFailure> configPluginFailures(const QString& configHome, const QStringList& skipRoots);

// `<configHome>/modules` when it is a folder, then each `extra` folder, absolute, without
// duplicates, only the existing folders.
QStringList importRoots(const QString& configHome, const QStringList& extra);

// The roots whose plugins all pass checkPluginFile. A refused plugin logs one line with the file
// and the reason, and its root is left out.
QStringList usableImportRoots(const QStringList& roots);

}  // namespace wabar::host
