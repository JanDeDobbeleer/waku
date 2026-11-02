// wakuc: the command-line client of a running bar.
//
//     wakuc reload     reload the config of the running bar
//     wakuc status     what the running bar is doing
//     wakuc quit       stop the running bar
//     wakuc wallpaper set [--dry-run] <file-or-path>
//                       set the wallpaper: a full path to any image, or a file name inside the
//                       wallpaper folder (case does not matter, the extension may be left out)
//     wakuc wallpaper random|next|previous [--dry-run]
//                       a random wallpaper of the folder, the one after or before the current
//     wakuc wallpaper gallery
//                       open or close the picker
//     wakuc shortcut <name>
//                       fire the GlobalShortcut with that name (bind a key to this command in
//                       whkd or AutoHotkey)
//     wakuc shortcut list
//                       the names of the shortcuts the bar has
//     wakuc surface list
//     wakuc surface show|hide|toggle|status <name>
//                       the windows of the shell that have a `name`: a launcher, an OSD, a menu.
//                       show and toggle first allow the bar to take the keyboard focus
//                       (AllowSetForegroundWindow), so a launcher opened from a hotkey gets it
//     wakuc taskbar hide|show|status
//                       hide the Windows taskbar (auto-hide on, windows hidden), show it again
//                       (restores what waku changed), or report "taskbar: hidden|visible|
//                       autohide-was-on". `waku --hide-taskbar` hides it at start
//     wakuc call [<target> <function> [args...]]
//                       run a function of an IpcHandler; the answer is its return value on one
//                       line ("ok" for void). Without arguments: the handlers and their functions
//     wakuc config init [--force]
//                       write a starter shell.qml (the built-in bar) into the config home so
//                       you can edit it. Works without a running bar; refuses to replace an
//                       existing shell.qml unless --force
//
// `--dry-run` plays the animation but does not set the wallpaper. A command is sent as one line;
// arguments with spaces are quoted (control.h, joinCommand).
//
// A separate console program, as komorebic is to komorebi: the bar itself is a Windows (GUI)
// program with no console to print to. `--config-home <dir>` (or $WAKU_CONFIG_HOME) selects
// the instance, as it does for the bar.
//
// Exit code: 0 the bar answered ok, 1 it answered an error, 2 no bar is running, 3 usage error or
// no answer.
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <cstdio>

#include "control/config_init.h"
#include "control/control.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

// The bar can only take the keyboard focus from the foreground app if this process, which the
// user's input (a hotkey) just started, allows it. The bar's pid is in its `status` answer.
void allowBarToTakeFocus(const QString& pipeName) {
#ifdef Q_OS_WIN
  const waku::control::ControlReply status =
      waku::control::sendControlCommand(pipeName, QStringLiteral("status"));
  if (status.status != waku::control::ControlReply::Ok) return;
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

// How long a command may go without an answer. The bar answers on its GUI thread, which a reload
// keeps busy for seconds (a command sent then is answered after it); a reload itself gets longer.
constexpr int kTimeoutMs = 15000;
constexpr int kReloadTimeoutMs = 60000;

// The starter shell is the built-in bar: waku's DefaultShell.qml, embedded (CMakeLists.txt).
QString readStarterTemplate() {
  QFile file(QStringLiteral(":/waku/DefaultShell.qml"));
  return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

void print(FILE* stream, const QString& text) {
  std::fputs(text.toLocal8Bit().constData(), stream);
  std::fputc('\n', stream);
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("wakuc"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral(
      "Controls a running waku bar.\n\nCommands:\n  reload  reload the config\n"
      "  status  show what the bar is doing\n  quit    stop the bar\n"
      "  wallpaper set <file-or-path>   a full path, or a file name in the wallpaper folder\n"
      "  wallpaper random | next | previous\n"
      "  wallpaper gallery              open or close the picker\n"
      "  shortcut <name>                fire the GlobalShortcut with that name\n"
      "  shortcut list                  the names of the shortcuts\n"
      "  surface list                   the named windows of the shell and their state\n"
      "  surface show | hide | toggle | status <name>\n"
      "  taskbar hide | show | status   hide or restore the Windows taskbar\n"
      "  config init [--force]          write a starter shell.qml into the config home\n"
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
  QCommandLineOption forceOpt(QStringLiteral("force"),
                              QStringLiteral("config init: replace an existing shell.qml."));
  parser.addOption(forceOpt);
  parser.addPositionalArgument(
      QStringLiteral("command"),
      QStringLiteral(
          "reload, status, quit, wallpaper, shortcut, surface, taskbar, config or call"));
  parser.process(app);

  const QStringList positional = parser.positionalArguments();
  const QString usage = QStringLiteral(
      "usage: wakuc [--config-home <dir>] reload|status|quit|wallpaper "
      "(set [--dry-run] <file-or-path>|random|next|previous|gallery)|shortcut <name>|list|surface "
      "(list|show|hide|toggle|status <name>)|taskbar (hide|show|status)|config init [--force]|call "
      "<target> <function> "
      "[args...]");
  if (positional.isEmpty()) {
    print(stderr, usage);
    return kUsage;
  }
  const QString command = positional.first();
  QString line = command;
  if (command == QLatin1String("config")) {
    // Client side: no bar, no pipe.
    if (positional.size() != 2 || positional.at(1) != QLatin1String("init") ||
        parser.isSet(dryRunOpt)) {
      print(stderr, usage);
      return kUsage;
    }
    const QString configHome =
        waku::control::resolveConfigHome(parser.value(configHomeOpt), QDir::homePath()).path;
    const QString text = waku::control::starterShellText(readStarterTemplate());
    if (text.isEmpty()) {
      print(stderr, QStringLiteral("wakuc: the starter shell is missing from this build"));
      return kBarError;
    }
    const waku::control::ConfigInitResult result =
        waku::control::initConfigHome(configHome, text, parser.isSet(forceOpt));
    if (result.status != waku::control::ConfigInitResult::Written) {
      print(stderr, result.message);
      return kBarError;
    }
    print(stdout, result.path);
    return kOk;
  }
  if (parser.isSet(forceOpt)) {
    print(stderr, usage);
    return kUsage;
  }
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
    line = waku::control::joinCommand(words);
  } else if (parser.isSet(dryRunOpt)) {
    print(stderr, usage);
    return kUsage;
  } else {
    // Any other command goes to the bar as typed: the bar knows its verbs and answers for an
    // unknown one ("error: unknown command ...").
    line = waku::control::joinCommand(positional);
  }

  // The same default as the bar, so that the two meet without any option.
  const QString configHome =
      waku::control::resolveConfigHome(parser.value(configHomeOpt), QDir::homePath()).path;
  const QString pipeName = waku::control::controlPipeName(configHome);
  if (command == QLatin1String("surface") && (positional.value(1) == QLatin1String("show") ||
                                              positional.value(1) == QLatin1String("toggle"))) {
    allowBarToTakeFocus(pipeName);
  }
  // A reload answers when the new shell is up, which takes seconds with a large shell.
  const int timeoutMs = command == QLatin1String("reload") ? kReloadTimeoutMs : kTimeoutMs;
  const waku::control::ControlReply reply =
      waku::control::sendControlCommand(pipeName, line, timeoutMs);

  switch (reply.status) {
    case waku::control::ControlReply::NotRunning:
      print(stderr, QStringLiteral("wakuc: no bar is running for %1").arg(configHome));
      return kNotRunning;
    case waku::control::ControlReply::Failed:
      print(stderr, QStringLiteral("wakuc: %1").arg(reply.text));
      return kUsage;
    case waku::control::ControlReply::Ok:
      break;
  }
  const bool error = reply.text.startsWith(QLatin1String("error:"));
  print(error ? stderr : stdout, reply.text);
  return error ? kBarError : kOk;
}
