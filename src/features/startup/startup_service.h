#pragma once

#include <QString>
#include <QStringList>

#include "features/startup/startup_system.h"

namespace waku::startup {

// What `wakuc startup enable` puts on the command line of waku.exe.
struct StartupOptions {
  // Empty: omit the flag; waku resolves its own default (WAKU_CONFIG_HOME, else ~/.config/waku).
  QString configHome;
  bool registerAppBar = true;
  bool hideTaskbar = true;
  QStringList extraArguments;
};

// The arguments of waku.exe: --config-home (when set), --register-app-bar, --hide-taskbar, then the
// extras. An extra flag that is already present is dropped.
QStringList wakuArguments(const StartupOptions& options);

// The arguments of wakukeys.exe: --config <keysFile>, then --config-home when set (the bar the
// `waku:` actions talk to).
QStringList keysArguments(const QString& keysFile, const QString& configHome);

// The arguments as one Windows command line (CommandLineToArgvW quoting rules).
QString commandLine(const QStringList& arguments);

// `lines` are one-line messages. A line that starts with "warning:" goes to stderr, the others to
// stdout; when `ok` is false they all go to stderr.
struct StartupResult {
  bool ok = false;
  QStringList lines;
};

// `enable` registers the bar task, then the keys daemon task when the default keys file exists.
// Without that file, or when wakukeys.exe is not on the PATH, the keys task is skipped with a line
// that says why; that is not an error. `disable` removes both tasks, `status` reports both.
StartupResult enableStartup(StartupSystem& system, const StartupOptions& options);
StartupResult disableStartup(StartupSystem& system);
StartupResult startupStatus(StartupSystem& system);

}  // namespace waku::startup
