// tidyc: the command-line client of a running bar.
//
//     tidyc reload     reload the config of the running bar
//     tidyc status     what the running bar is doing
//     tidyc quit       stop the running bar
//
// A separate console program, as komorebic is to komorebi: the bar itself is a Windows (GUI)
// program with no console to print to. `--config-home <dir>` (or $TIDY_CONFIG_HOME) selects
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
  QCoreApplication::setApplicationName(QStringLiteral("tidyc"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Controls a running tidy bar.\n\nCommands:\n  reload  reload the config\n"
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
    print(stderr, QStringLiteral("usage: tidyc [--config-home <dir>] reload|status|quit"));
    return kUsage;
  }
  const QString command = positional.first();
  if (command != QLatin1String("reload") && command != QLatin1String("status") &&
      command != QLatin1String("quit")) {
    print(stderr,
          QStringLiteral("tidyc: unknown command '%1' (try: reload, status, quit)").arg(command));
    return kUsage;
  }

  // The same default as the bar, so that the two meet without any option.
  const QString configHome =
      tidy::resolveConfigHome(parser.value(configHomeOpt), QStringLiteral(SPIKE_CONFIG_DIR));
  const tidy::ControlReply reply =
      tidy::sendControlCommand(tidy::controlPipeName(configHome), command);

  switch (reply.status) {
    case tidy::ControlReply::NotRunning:
      print(stderr, QStringLiteral("tidyc: no bar is running for %1").arg(configHome));
      return kNotRunning;
    case tidy::ControlReply::Failed:
      print(stderr, QStringLiteral("tidyc: %1").arg(reply.text));
      return kUsage;
    case tidy::ControlReply::Ok:
      break;
  }
  const bool error = reply.text.startsWith(QLatin1String("error:"));
  print(error ? stderr : stdout, reply.text);
  return error ? kBarError : kOk;
}
