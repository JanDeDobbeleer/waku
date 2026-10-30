// wabarc: the command-line client of a running bar.
//
//     wabarc reload     reload the config of the running bar
//     wabarc status     what the running bar is doing
//     wabarc quit       stop the running bar
//     wabarc wallpaper set [--dry-run] <file-or-path>
//                       set the wallpaper: a full path to any image, or a file name inside the
//                       wallpaper folder (case does not matter, the extension may be left out)
//     wabarc wallpaper random|next|previous [--dry-run]
//                       a random wallpaper of the folder, the one after or before the current
//     wabarc wallpaper gallery
//                       open or close the picker
//     wabarc shortcut <name>
//                       fire the GlobalShortcut with that name (bind a key to this command in
//                       whkd or AutoHotkey)
//     wabarc shortcut list
//                       the names of the shortcuts the bar has
//     wabarc surface list
//     wabarc surface show|hide|toggle|status <name>
//                       the windows of the shell that have a `name`: a launcher, an OSD, a menu.
//                       show and toggle first allow the bar to take the keyboard focus
//                       (AllowSetForegroundWindow), so a launcher opened from a hotkey gets it
//     wabarc taskbar hide|show|status
//                       hide the Windows taskbar (auto-hide on, windows hidden), show it again
//                       (restores what wabar changed), or report "taskbar: hidden|visible|
//                       autohide-was-on". `wabar --hide-taskbar` hides it at start
//     wabarc call [<target> <function> [args...]]
//                       run a function of an IpcHandler; the answer is its return value on one
//                       line ("ok" for void). Without arguments: the handlers and their functions
//
// `--dry-run` plays the animation but does not set the wallpaper. A command is sent as one line;
// arguments with spaces are quoted (control.h, joinCommand).
//
// A separate console program, as komorebic is to komorebi: the bar itself is a Windows (GUI)
// program with no console to print to. `--config-home <dir>` (or $WABAR_CONFIG_HOME) selects
// the instance, as it does for the bar.
//
// Exit code: 0 the bar answered ok, 1 it answered an error, 2 no bar is running, 3 usage error or
// no answer.
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <cstdio>

#include "control/control.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// The bar can only take the keyboard focus from the foreground app if this process, which the
// user's input (a hotkey) just started, allows it. The bar's pid is in its `status` answer.
void allowBarToTakeFocus(const QString& pipeName) {
#ifdef Q_OS_WIN
  const wabar::control::ControlReply status =
      wabar::control::sendControlCommand(pipeName, QStringLiteral("status"));
  if (status.status != wabar::control::ControlReply::Ok) return;
  const QRegularExpressionMatch match =
      QRegularExpression(QStringLiteral("\\bpid=(\\d+)")).match(status.text);
  if (!match.hasMatch()) return;
  AllowSetForegroundWindow(static_cast<DWORD>(match.captured(1).toULong()));
#else
  Q_UNUSED(pipeName);
#endif
}

constexpr int kOk = 0;
constexpr int kBarError = 1;
constexpr int kNotRunning = 2;
constexpr int kUsage = 3;

void print(FILE* stream, const QString& text) {
  std::fputs(text.toLocal8Bit().constData(), stream);
  std::fputc('\n', stream);
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("wabarc"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral(
      "Controls a running wabar bar.\n\nCommands:\n  reload  reload the config\n"
      "  status  show what the bar is doing\n  quit    stop the bar\n"
      "  wallpaper set <file-or-path>   a full path, or a file name in the wallpaper folder\n"
      "  wallpaper random | next | previous\n"
      "  wallpaper gallery              open or close the picker\n"
      "  shortcut <name>                fire the GlobalShortcut with that name\n"
      "  shortcut list                  the names of the shortcuts\n"
      "  surface list                   the named windows of the shell and their state\n"
      "  surface show | hide | toggle | status <name>\n"
      "  taskbar hide | show | status   hide or restore the Windows taskbar\n"
      "  call <target> <function> [args...]  run a function of an IpcHandler; call alone lists\n"
      "Add --dry-run to set, random, next or previous to play the animation without "
      "changing the wallpaper."));
  parser.addHelpOption();
  QCommandLineOption configHomeOpt(QStringLiteral("config-home"),
                                   QStringLiteral("Config folder of the bar to talk to."),
                                   QStringLiteral("dir"));
  parser.addOption(configHomeOpt);
  QCommandLineOption dryRunOpt(QStringLiteral("dry-run"),
                               QStringLiteral("wallpaper: play the animation, do not set it."));
  parser.addOption(dryRunOpt);
  parser.addPositionalArgument(
      QStringLiteral("command"),
      QStringLiteral("reload, status, quit, wallpaper, shortcut, surface, taskbar or call"));
  parser.process(app);

  const QStringList positional = parser.positionalArguments();
  const QString usage = QStringLiteral(
      "usage: wabarc [--config-home <dir>] reload|status|quit|wallpaper "
      "(set [--dry-run] <file-or-path>|random|next|previous|gallery)|shortcut <name>|list|surface "
      "(list|show|hide|toggle|status <name>)|taskbar (hide|show|status)|call <target> <function> "
      "[args...]");
  if (positional.isEmpty()) {
    print(stderr, usage);
    return kUsage;
  }
  const QString command = positional.first();
  QString line = command;
  if (command == QLatin1String("wallpaper")) {
    const QString verb = positional.value(1);
    const bool known = verb == QLatin1String("set") || verb == QLatin1String("random") ||
                       verb == QLatin1String("next") || verb == QLatin1String("previous") ||
                       verb == QLatin1String("gallery");
    const qsizetype wanted = verb == QLatin1String("set") ? 3 : 2;
    if (!known || positional.size() != wanted ||
        (verb == QLatin1String("gallery") && parser.isSet(dryRunOpt))) {
      print(stderr, usage);
      return kUsage;
    }
    QStringList words{command, verb};
    if (parser.isSet(dryRunOpt)) words << QStringLiteral("--dry-run");
    if (verb == QLatin1String("set")) {
      // The bar has another working folder: a path with a folder part is made absolute here.
      QString target = positional.at(2);
      if (target.contains(QLatin1Char('/')) || target.contains(QLatin1Char('\\')) ||
          target.contains(QLatin1Char(':'))) {
        target = QDir::toNativeSeparators(QDir::current().absoluteFilePath(target));
      }
      words << target;
    }
    line = wabar::control::joinCommand(words);
  } else if (parser.isSet(dryRunOpt)) {
    print(stderr, usage);
    return kUsage;
  } else {
    // Any other command goes to the bar as typed: the bar knows its verbs and answers for an
    // unknown one ("error: unknown command ...").
    line = wabar::control::joinCommand(positional);
  }

  // The same default as the bar, so that the two meet without any option.
  const QString configHome = wabar::control::resolveConfigHome(
      parser.value(configHomeOpt), QStringLiteral(WABAR_SAMPLE_CONFIG_DIR));
  const QString pipeName = wabar::control::controlPipeName(configHome);
  if (command == QLatin1String("surface") && (positional.value(1) == QLatin1String("show") ||
                                              positional.value(1) == QLatin1String("toggle"))) {
    allowBarToTakeFocus(pipeName);
  }
  const wabar::control::ControlReply reply = wabar::control::sendControlCommand(pipeName, line);

  switch (reply.status) {
    case wabar::control::ControlReply::NotRunning:
      print(stderr, QStringLiteral("wabarc: no bar is running for %1").arg(configHome));
      return kNotRunning;
    case wabar::control::ControlReply::Failed:
      print(stderr, QStringLiteral("wabarc: %1").arg(reply.text));
      return kUsage;
    case wabar::control::ControlReply::Ok:
      break;
  }
  const bool error = reply.text.startsWith(QLatin1String("error:"));
  print(error ? stderr : stdout, reply.text);
  return error ? kBarError : kOk;
}
