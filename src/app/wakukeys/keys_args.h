#pragma once

#include <QString>
#include <QStringList>

namespace wakukeys {

// The command line of wakukeys.exe:
//   wakukeys [--config <file>] [--config-home <dir>] [--log-file <file>]   start the daemon
//   wakukeys reload|stop|status                                            talk to it
// Pure: no process state, so it is tested without starting anything.
struct KeysArgs {
  QString verb;        // "" (start), "reload", "stop" or "status"
  QString configFile;  // --config; empty: %USERPROFILE%\.config\waku\keys.conf if it exists
  QString logFile;     // --log-file; empty: no file
  QString configHome;  // --config-home: which bar `waku:` actions talk to
  QString error;       // not empty: a usage error, the text to print
};

// `arguments` is argv without the program name. `--opt value` and `--opt=value` both work.
KeysArgs parseKeysArgs(const QStringList& arguments);

// The usage line.
QString keysUsage();

}  // namespace wakukeys
