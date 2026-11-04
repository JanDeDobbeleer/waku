#pragma once

#include <QString>
#include <QStringList>
#include <optional>

#include "features/keymap/keymap_types.h"

namespace waku::keymap {

// What a binding does. A command that does not start with `waku:` is a shell command; the others
// are built in:
//
//   waku:shortcut <name>                       bar: `shortcut <name>`
//   waku:surface <show|hide|toggle> <name>     bar: `surface <verb> <name>`
//   waku:call <target> <function> [args...]    bar: `call ...`
//   waku:reload                                bar: `reload`
//   waku:keys-reload / waku:keys-stop          this daemon
//   waku:activate-or-launch <exe-or-title> <command>
struct Action {
  enum class Kind { Shell, Bar, KeysReload, KeysStop, ActivateOrLaunch };
  Kind kind = Kind::Shell;
  QString text;    // Shell: the command. Bar: the control line. ActivateOrLaunch: the command.
  QString target;  // ActivateOrLaunch: the executable or title to look for
};

struct ActionParse {
  std::optional<Action> action;
  QString error;  // set when action is empty
};

// Pure. A `waku:` prefix is case-insensitive. Arguments are split like a `wakuc` command line
// (control.h, splitCommand): double quotes group, a doubled quote inside is a quote.
ActionParse parseAction(const QString& command);

// How to start a shell command: the program and its arguments, or `nativeArgs` for CreateProcess
// verbatim (cmd.exe). PowerShell gets the command as -EncodedCommand, so no quoting can break it.
struct ShellInvocation {
  QString program;
  QStringList args;
  QString nativeArgs;
};
ShellInvocation shellInvocation(Shell shell, const QString& command);

// The command a binding runs when `foregroundProcess` (e.g. "firefox.exe", "" when unknown) is in
// front: the plain command, or the matching rule of a block, else its Default. Empty when the
// binding does nothing here (no rule, `Ignore`, or the app is on the blacklist). `reason`, when
// given, says why in the last case.
QString commandForForeground(const Binding& binding, const QStringList& blacklist,
                             const QString& foregroundProcess, QString* reason = nullptr);

}  // namespace waku::keymap
