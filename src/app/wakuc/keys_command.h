#pragma once

#include <QString>

namespace wakuc {

// `wakuc keys [--config <file>] [--foreground] [--log-file <file>]` and `wakuc keys
// reload|stop|status`: the hotkey daemon (features/keymap) and its control commands. Linked into
// wakuc only, never into waku.exe.
struct KeysOptions {
  QString verb;        // "" (start), "reload", "stop" or "status"
  QString configFile;  // --config; empty: %USERPROFILE%\.config\waku\keys.conf if it exists
  QString logFile;     // --log-file; empty: no file
  QString configHome;  // --config-home: which bar `waku:` actions talk to
  bool foreground = false;
};

// Exit codes as in main.cpp: 0 ok, 1 error, 2 daemon not running, 3 usage.
int runKeysCommand(const KeysOptions& options);

// The control pipe of the daemon of the signed-in user.
QString keysPipeName();

}  // namespace wakuc
