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

// The arguments as one Windows command line (CommandLineToArgvW quoting rules).
QString commandLine(const QStringList& arguments);

// `lines` are one-line messages. A line that starts with "warning:" goes to stderr, the others to
// stdout; when `ok` is false they all go to stderr.
struct StartupResult {
  bool ok = false;
  QStringList lines;
};

StartupResult enableStartup(StartupSystem& system, const StartupOptions& options);
StartupResult disableStartup(StartupSystem& system);
StartupResult startupStatus(StartupSystem& system);

}  // namespace waku::startup
