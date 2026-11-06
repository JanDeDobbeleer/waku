// wakukeys.exe: the hotkey daemon (features/keymap) and its control commands. A Windows (GUI)
// program: no console window, so a login task or a script starts it without a flash and a closed
// terminal does not end it. Subcommands print to the console of the terminal that ran them.
//
//     wakukeys [--config <file>] [--config-home <dir>] [--log-file <file>]
//         start the daemon: loads a whkd binding file (`alt + h : command`), registers the keys
//         with RegisterHotKey and runs the commands. One daemon per user; a second start exits.
//         --config defaults to %USERPROFILE%\.config\waku\keys.conf when that file exists.
//         --config-home picks the bar that `waku:` actions talk to; --log-file writes the log.
//     wakukeys reload|stop|status
//         reload the file now, stop the daemon, or print "ok: running; pid=P; bindings=N;
//         conflicts=C; issues=I; paused=no|yes; config=F".
//
// Exit code: 0 ok, 1 error, 2 the daemon is not running, 3 usage. Syntax and decisions:
// src/features/keymap/design.md.
#include <QCoreApplication>

#include "app/wakukeys/keys_args.h"
#include "app/wakukeys/keys_daemon.h"

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("wakukeys"));
  QStringList arguments = app.arguments();
  arguments.removeFirst();
  return wakukeys::runKeys(wakukeys::parseKeysArgs(arguments));
}
