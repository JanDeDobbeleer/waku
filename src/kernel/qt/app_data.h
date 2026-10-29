#pragma once

#include <QString>
#include <QStringList>

namespace wabar::kernel {

// wabar's one data folder, %LOCALAPPDATA%\wabar. Every file wabar keeps (taskbar pins, the GitHub
// token, tray state, wallpaper thumbnails) lives under it.

// %LOCALAPPDATA%, or <home>/AppData/Local when the variable is not set. Read on every call so a
// test can point it at a temporary folder.
QString localAppDataBase();

// <localAppDataBase()>/wabar, created on demand. The first call for a base folder also runs
// migrateLegacyAppData() on it (once per process and base), so no caller sees the folder before
// the copy is done. Thread-safe.
QString appDataDir();

// What migrateLegacyAppData() did.
struct AppDataMigration {
  bool ran = false;     // false: nothing to do (no YASB folder, or already migrated)
  QStringList copied;   // file names copied from YASB into wabar
  QStringList skipped;  // names left alone because wabar already has such a file
};

// One-time copy from the former data folder %LOCALAPPDATA%\YASB into %LOCALAPPDATA%\wabar of
// taskbar_pinned.json, github_token and systray_state_*.json. The Widgets build and Python YASB
// own the YASB folder: files are copied, never moved, never overwritten in wabar, and YASB is
// never written. Does nothing when YASB does not exist. Idempotent: it drops a marker file
// (migrated_from_yasb) in wabar after the pass, and later calls return {} at once, so a file the
// user deletes from wabar does not come back. Logs one line when it copied something.
AppDataMigration migrateLegacyAppData(const QString& base);

}  // namespace wabar::kernel
