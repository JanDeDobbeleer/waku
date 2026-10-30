#pragma once

#include <QString>
#include <QStringList>

namespace wabar::host {

class ShellHost;

// The `surface` commands of the control channel (the words after "surface"), answered for the
// windows of `host` that have a `name`:
//   list                       "ok: launcher=hidden, bar=shown (2 windows)" or "ok: none"
//   show|hide|toggle <name>    "ok: <name> shown|hidden"; toggle hides when any window of the name
//                              is shown, else shows all of them
//   status <name>              "ok: <name> shown|hidden", plus " (N windows)" when N > 1
// A verb applies to every window of the name (one per screen). "shown" means `visible` is true.
// Errors: "error: usage: ...", "error: unknown surface command '...'", "error: no surface named
// '...' (known: ...)". Public for tests.
QString answerSurfaceCommand(const QStringList& words, ShellHost* host);

}  // namespace wabar::host
