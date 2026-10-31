#pragma once

#include <QStringList>
#include <functional>
#include <map>

#include "control/control.h"

namespace wabar::host {

class ShellHost;
class WindowsTaskbar;

// A command that a unit outside the host adds to the control channel, such as a feature's verb:
// it gets the words after the verb (split as control::splitCommand does) and returns the answer
// line, "ok: <text>" or "error: <text>". The composition root builds the table, so the host
// knows no feature's vocabulary.
using ControlCommand = std::function<QString(const QStringList& words)>;
using ControlCommands = std::map<QString, ControlCommand>;

// What a running shell answers on its control channel:
//   reload  reloads the shell: "ok", or "error: <first message>" (when the new shell was rejected
//           the previous one stays; when some of its windows failed the others are shown)
//   status  "running; pid=P; windows=N; safe=yes|no; error=<none or the message>" (`wabarc` reads
//           the pid to call AllowSetForegroundWindow before `surface show|toggle`)
//   quit    "ok", then `quit` runs shortly after, so the answer is written first
//   shortcut <name>  fires the GlobalShortcut with that name: "ok", or "error: no shortcut named
//           '<name>'"; `shortcut list` answers "ok: <names>" (quoted as joinCommand does)
//   surface list | show|hide|toggle|status <name>
//           the named windows of the shell (surface_control.h): "ok: <text>" or "error: <text>"
//   taskbar hide | show | status
//           hides or restores the Windows taskbar (windows_taskbar_command.h): "ok: taskbar:
//           <state>"
//   call [<target> <function> [args...]]
//           runs a function of an IpcHandler (ipc_handler.h): the return value as one line, "ok"
//           for void, "error: <text>"; without words the handlers and their functions
//   <verb> [words...]
//           a verb of `commands`, which gets the words after it (wabar.exe adds `wallpaper`);
//           a built-in verb above wins over a command of the same name
//   other   "error: unknown command '...' (try: reload, status, quit, shortcut, surface, taskbar,
//           call, <the verbs of commands>)"
// `quit` defaults to QCoreApplication::quit. A command runs only when its verb arrives, so a
// service it asks for (the wallpapers) can be created then.
// `taskbar` answers the `taskbar` verbs; null (the offscreen platform) answers an error.
control::ControlHandler makeControlHandler(ShellHost& host, bool safe,
                                           std::function<void()> quit = {},
                                           ControlCommands commands = {},
                                           WindowsTaskbar* taskbar = nullptr);

// The same answers through the control server's deferred replies: `reload` runs from a queued
// call on the GUI thread, after the socket's read notification has returned, and answers when the
// reload is done; every other command answers at once.
control::AsyncControlHandler makeAsyncControlHandler(control::ControlHandler handler);

// The `shortcut` command on its own (the words after "shortcut"), public for tests.
QString answerShortcutCommand(const QStringList& words);

}  // namespace wabar::host
