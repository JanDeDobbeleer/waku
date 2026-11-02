#pragma once

#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

namespace waku::apps {

enum class AppKind {
  Shortcut,  // a .lnk file under a Start Menu folder
  Store,     // a packaged app of shell:AppsFolder, launched by its AUMID
};

// One launchable app, in the shape of a freedesktop entry.
struct AppRecord {
  // Stable key: the shortcut's path below its Start Menu folder without ".lnk", with "/"
  // separators ("Visual Studio Code/Visual Studio Code"), or the AUMID of a Store app.
  QString id;
  QString name;
  QString genericName;
  QString comment;
  AppKind kind = AppKind::Shortcut;
  QString path;  // the .lnk file (shortcuts)
  QString target;
  QString arguments;
  QString workingDirectory;
  QString aumid;       // Store apps
  QString iconSource;  // what the shell icon is read from: the .lnk, or shell:AppsFolder\<aumid>
  QStringList categories;
  QStringList keywords;

  bool operator==(const AppRecord&) const = default;
};

// What a shortcut file says. `ok` is false when it could not be read.
struct ShortcutInfo {
  bool ok = false;
  QString target;
  QString arguments;
  QString workingDirectory;
  QString description;
};

// Reads one .lnk file. Real: IShellLink (win32/start_menu_win.cpp). Tests pass a fake.
using ShortcutResolver = std::function<ShortcutInfo(const QString& lnkPath)>;
// Lists the Store apps of shell:AppsFolder. Real: win32/apps_folder_win.cpp. Runs off the GUI
// thread.
using AppsFolderEnumerator = std::function<std::vector<AppRecord>()>;

// True for shortcuts that are not apps: names containing "uninstall" or "readme", or the word
// "help" ("Help", "Documentation and Help"; not "Helper").
bool isNoiseName(const QString& name);

// The ".lnk" files below `folders` (recursive, in the order of the folders), without hidden files
// and noise names, resolved with `resolver`. A shortcut that appears in several folders (the user
// and the common Start Menu) is kept once, from the first folder. Unreadable shortcuts are
// skipped.
std::vector<AppRecord> scanShortcuts(const QStringList& folders, const ShortcutResolver& resolver);

// Shortcuts and Store apps as one list sorted by name (case-insensitive, then id). Drops Store
// apps without a name, noise names and ids that repeat, and a Store app whose name a shortcut
// already has.
std::vector<AppRecord> buildCatalog(std::vector<AppRecord> shortcuts,
                                    std::vector<AppRecord> storeApps);

// `folders` and every folder below them: the folders a watcher needs to see a new shortcut.
QStringList watchedDirectories(const QStringList& folders);

// `"target" arguments` for a shortcut, `shell:AppsFolder\<aumid>` for a Store app.
QString execStringFor(const AppRecord& record);
// The execString as an argument list: [target, ...arguments] or ["explorer.exe", shell:...].
QStringList commandFor(const AppRecord& record);
// The target's file name without extension ("code" for "C:\...\Code.exe"), "" without a target.
QString startupClassFor(const AppRecord& record);

}  // namespace waku::apps
