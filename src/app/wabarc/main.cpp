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
#include <cstdio>

#include "control/control.h"

namespace {

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
  parser.addPositionalArgument(QStringLiteral("command"),
                               QStringLiteral("reload, status, quit or wallpaper"));
  parser.process(app);

  const QStringList positional = parser.positionalArguments();
  const QString usage = QStringLiteral(
      "usage: wabarc [--config-home <dir>] reload|status|quit|wallpaper "
      "(set [--dry-run] <file-or-path>|random|next|previous|gallery)");
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
  } else if (positional.size() != 1 ||
             (command != QLatin1String("reload") && command != QLatin1String("status") &&
              command != QLatin1String("quit"))) {
    print(stderr, QStringLiteral("wabarc: unknown command '%1' (try: reload, status, quit, "
                                 "wallpaper)")
                      .arg(command));
    return kUsage;
  } else if (parser.isSet(dryRunOpt)) {
    print(stderr, usage);
    return kUsage;
  }

  // The same default as the bar, so that the two meet without any option.
  const QString configHome = wabar::control::resolveConfigHome(
      parser.value(configHomeOpt), QStringLiteral(WABAR_SAMPLE_CONFIG_DIR));
  const wabar::control::ControlReply reply =
      wabar::control::sendControlCommand(wabar::control::controlPipeName(configHome), line);

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
