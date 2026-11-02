#pragma once

#include <QString>
#include <QStringList>

namespace waku::kernel {

// waku's one data folder, %LOCALAPPDATA%\waku. Every file waku keeps (taskbar pins, the GitHub
// token, tray state, wallpaper thumbnails) lives under it.

// %LOCALAPPDATA%, or <home>/AppData/Local when the variable is not set. Read on every call so a
// test can point it at a temporary folder.
QString localAppDataBase();

// <localAppDataBase()>/waku, created on demand. The first call for a base folder also runs
// migrateLegacyAppData() on it (once per process and base), so no caller sees the folder before
// the copy is done. Thread-safe.
QString appDataDir();

// What migrateLegacyAppData() did.
struct AppDataMigration {
  bool ran = false;     // false: nothing to do (no YASB folder, or already migrated)
  QStringList copied;   // file names copied from YASB into waku
  QStringList skipped;  // names left alone because waku already has such a file
};

// One-time copy from the former data folder %LOCALAPPDATA%\YASB into %LOCALAPPDATA%\waku of
// taskbar_pinned.json, github_token and systray_state_*.json. The Widgets build and Python YASB
// own the YASB folder: files are copied, never moved, never overwritten in waku, and YASB is
// never written. Does nothing when YASB does not exist. Idempotent: it drops a marker file
// (migrated_from_yasb) in waku after the pass, and later calls return {} at once, so a file the
// user deletes from waku does not come back. Logs one line when it copied something.
AppDataMigration migrateLegacyAppData(const QString& base);

}  // namespace waku::kernel
