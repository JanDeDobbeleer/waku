#pragma once

#include <functional>

#include "control/control.h"
#include "control/wallpaper_control.h"

namespace wabar::host {

class ShellHost;

// What a running shell answers on its control channel:
//   reload  reloads the shell: "ok", or "error: <first message>" (when the new shell was rejected
//           the previous one stays; when some of its windows failed the others are shown)
//   status  "running; pid=P; windows=N; safe=yes|no; error=<none or the message>" (`wabarc` reads
//           the pid to call AllowSetForegroundWindow before `surface show|toggle`)
//   quit    "ok", then `quit` runs shortly after, so the answer is written first
//   wallpaper set [--dry-run] <file-or-path> | random | next | previous [--dry-run] | gallery
//           answered by the wallpaper service: "ok: <text>" or "error: <text>". The words follow
//           the quoting of splitCommand (control.h). Without a service: "error: ..."
//   shortcut <name>  fires the GlobalShortcut with that name: "ok", or "error: no shortcut named
//           '<name>'"; `shortcut list` answers "ok: <names>" (quoted as joinCommand does)
//   other   "error: unknown command '...' (try: reload, status, quit, wallpaper, shortcut)"
//   surface list | show|hide|toggle|status <name>
//           the named windows of the shell (surface_control.h): "ok: <text>" or "error: <text>"
//   call [<target> <function> [args...]]
//           runs a function of an IpcHandler (ipc_handler.h): the return value as one line, "ok"
//           for void, "error: <text>"; without words the handlers and their functions
//   other   "error: unknown command '...' (try: reload, status, quit, wallpaper, shortcut,
//           surface, call)"
// `quit` defaults to QCoreApplication::quit. `wallpapers` is asked when a wallpaper command
// arrives (the service is created on demand), so a shell without wallpapers never builds it.
control::ControlHandler makeControlHandler(ShellHost& host, bool safe,
                                           std::function<void()> quit = {},
                                           control::WallpaperControlProvider wallpapers = {});

// The `wallpaper` commands on their own (the words after "wallpaper"): the parsing the handler
// uses, public for tests.
QString answerWallpaperCommand(const QStringList& words, control::WallpaperControl* wallpapers);

// The `shortcut` command on its own (the words after "shortcut"), public for tests.
QString answerShortcutCommand(const QStringList& words);

}  // namespace wabar::host
