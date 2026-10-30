#pragma once

#include <functional>

#include "control/control.h"
#include "control/wallpaper_control.h"

namespace wabar::host {

class ShellHost;

// What a running shell answers on its control channel:
//   reload  reloads the shell: "ok", or "error: <first message>" (when the new shell was rejected
//           the previous one stays; when some of its windows failed the others are shown)
//   status  "running; windows=N; safe=yes|no; error=<none or the message>"
//   quit    "ok", then `quit` runs shortly after, so the answer is written first
//   wallpaper set [--dry-run] <file-or-path> | random | next | previous [--dry-run] | gallery
//           answered by the wallpaper service: "ok: <text>" or "error: <text>". The words follow
//           the quoting of splitCommand (control.h). Without a service: "error: ..."
//   other   "error: unknown command '...' (try: reload, status, quit, wallpaper)"
// `quit` defaults to QCoreApplication::quit. `wallpapers` is asked when a wallpaper command
// arrives (the service is created on demand), so a shell without wallpapers never builds it.
control::ControlHandler makeControlHandler(ShellHost& host, bool safe,
                                           std::function<void()> quit = {},
                                           control::WallpaperControlProvider wallpapers = {});

// The `wallpaper` commands on their own (the words after "wallpaper"): the parsing the handler
// uses, public for tests.
QString answerWallpaperCommand(const QStringList& words, control::WallpaperControl* wallpapers);

}  // namespace wabar::host
