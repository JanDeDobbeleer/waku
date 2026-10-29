// wabarc: the command-line client of a running bar.
//
//     wabarc reload     reload the config of the running bar
//     wabarc status     what the running bar is doing
//     wabarc quit       stop the running bar
//
// A separate console program, as komorebic is to komorebi: the bar itself is a Windows (GUI)
// program with no console to print to. `--config-home <dir>` (or $WABAR_CONFIG_HOME) selects
// the instance, as it does for the bar.
//
// Exit code: 0 the bar answered ok, 1 it answered an error, 2 no bar is running, 3 usage error or
// no answer.
#include <QCommandLineParser>
#include <QCoreApplication>
#include <cstdio>

#include "spike/control/control.h"

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
  parser.setApplicationDescription(
      QStringLiteral("Controls a running wabar bar.\n\nCommands:\n  reload  reload the config\n"
                     "  status  show what the bar is doing\n  quit    stop the bar"));
  parser.addHelpOption();
  QCommandLineOption configHomeOpt(QStringLiteral("config-home"),
                                   QStringLiteral("Config folder of the bar to talk to."),
                                   QStringLiteral("dir"));
  parser.addOption(configHomeOpt);
  parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("reload, status or quit"));
  parser.process(app);

  const QStringList positional = parser.positionalArguments();
  if (positional.size() != 1) {
    print(stderr, QStringLiteral("usage: wabarc [--config-home <dir>] reload|status|quit"));
    return kUsage;
  }
  const QString command = positional.first();
  if (command != QLatin1String("reload") && command != QLatin1String("status") &&
      command != QLatin1String("quit")) {
    print(
        stderr,
        QStringLiteral("wabarc: unknown command '%1' (try: reload, status, quit)").arg(command));
    return kUsage;
  }

  // The same default as the bar, so that the two meet without any option.
  const QString configHome =
      wabar::resolveConfigHome(parser.value(configHomeOpt), QStringLiteral(SPIKE_CONFIG_DIR));
  const wabar::ControlReply reply =
      wabar::sendControlCommand(wabar::controlPipeName(configHome), command);

  switch (reply.status) {
    case wabar::ControlReply::NotRunning:
      print(stderr, QStringLiteral("wabarc: no bar is running for %1").arg(configHome));
      return kNotRunning;
    case wabar::ControlReply::Failed:
      print(stderr, QStringLiteral("wabarc: %1").arg(reply.text));
      return kUsage;
    case wabar::ControlReply::Ok:
      break;
  }
  const bool error = reply.text.startsWith(QLatin1String("error:"));
  print(error ? stderr : stdout, reply.text);
  return error ? kBarError : kOk;
}
