#pragma once

#include <QString>

namespace wabar::kernel {

// The Start menu entry of a packaged or shortcut app: its display name and the exe it launches.
struct ShellApp {
  QString name;
  QString exe;
};

// The process image name (for example "msedge.exe") that runs `aumid`, "" when it is not found.
QString exeNameForAumid(const QString& aumid);

// shell:AppsFolder item of `aumid`; cached, empty fields when the app has no entry.
ShellApp resolveShellAppByAumid(const QString& aumid);

// FileDescription or ProductName of the first running process called `exeName`, "" when none.
QString appNameForExe(const QString& exeName);

// FileDescription or ProductName of the process that owns a visible window with this AUMID.
QString appNameForWindowAumid(const QString& aumid);

// Restores and raises the window of `aumid` (falling back to its process tree). False when the
// app has no window. GUI thread.
bool activateAppByAumid(const QString& aumid);

}  // namespace wabar::kernel
