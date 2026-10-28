// barnaclec: the command-line client of a running bar.
//
//     barnaclec reload     reload the config of the running bar
//     barnaclec status     what the running bar is doing
//     barnaclec quit       stop the running bar
//
// A separate console program, as komorebic is to komorebi: the bar itself is a Windows (GUI)
// program with no console to print to. `--config-home <dir>` (or $BARNACLE_CONFIG_HOME) selects
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
  QCoreApplication::setApplicationName(QStringLiteral("barnaclec"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Controls a running barnacle bar.\n\nCommands:\n  reload  reload the config\n"
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
    print(stderr, QStringLiteral("usage: barnaclec [--config-home <dir>] reload|status|quit"));
    return kUsage;
  }
  const QString command = positional.first();
  if (command != QLatin1String("reload") && command != QLatin1String("status") &&
      command != QLatin1String("quit")) {
    print(
        stderr,
        QStringLiteral("barnaclec: unknown command '%1' (try: reload, status, quit)").arg(command));
    return kUsage;
  }

  // The same default as the bar, so that the two meet without any option.
  const QString configHome =
      barnacle::resolveConfigHome(parser.value(configHomeOpt), QStringLiteral(SPIKE_CONFIG_DIR));
  const barnacle::ControlReply reply =
      barnacle::sendControlCommand(barnacle::controlPipeName(configHome), command);

  switch (reply.status) {
    case barnacle::ControlReply::NotRunning:
      print(stderr, QStringLiteral("barnaclec: no bar is running for %1").arg(configHome));
      return kNotRunning;
    case barnacle::ControlReply::Failed:
      print(stderr, QStringLiteral("barnaclec: %1").arg(reply.text));
      return kUsage;
    case barnacle::ControlReply::Ok:
      break;
  }
  const bool error = reply.text.startsWith(QLatin1String("error:"));
  print(error ? stderr : stdout, reply.text);
  return error ? kBarError : kOk;
}
