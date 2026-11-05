#include "features/startup/startup_service.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace waku::startup {

namespace {

QString exeName() { return QStringLiteral("waku.exe"); }
QString keysExeName() { return QStringLiteral("wakukeys.exe"); }

// A folder compared case-insensitively with native separators and no trailing separator.
QString normalizedFolder(const QString& folder) {
  QString text = QDir::toNativeSeparators(QDir::cleanPath(folder)).toLower();
  while (text.size() > 3 && text.endsWith(QLatin1Char('\\'))) text.chop(1);
  return text;
}

QString quoteArgument(const QString& argument) {
  const bool plain = !argument.isEmpty() && !argument.contains(QLatin1Char(' ')) &&
                     !argument.contains(QLatin1Char('\t')) && !argument.contains(QLatin1Char('"'));
  if (plain) return argument;
  QString out = QStringLiteral("\"");
  qsizetype backslashes = 0;
  for (const QChar c : argument) {
    if (c == QLatin1Char('\\')) {
      ++backslashes;
      continue;
    }
    if (c == QLatin1Char('"')) {
      out.append(QString(backslashes * 2 + 1, QLatin1Char('\\')));
    } else {
      out.append(QString(backslashes, QLatin1Char('\\')));
    }
    out.append(c);
    backslashes = 0;
  }
  out.append(QString(backslashes * 2, QLatin1Char('\\')));
  out.append(QLatin1Char('"'));
  return out;
}

QString programWithArguments(const QString& program, const QString& arguments) {
  return arguments.isEmpty() ? program : program + QLatin1Char(' ') + arguments;
}

QString programWithArguments(const QString& arguments) {
  return programWithArguments(exeName(), arguments);
}

using Task = StartupSystem::Task;

// Registers the keys daemon task. Adds its lines to `result`; false on a registration error.
bool enableKeys(StartupSystem& system, const StartupOptions& options, StartupResult& result) {
  if (!system.keysConfigExists(options.configHome)) {
    result.lines << QStringLiteral(
                        "startup: keys daemon not registered, no keys file at %1 (create it and "
                        "run this command again)")
                        .arg(QDir::toNativeSeparators(system.keysConfigPath(options.configHome)));
    return true;
  }
  const QString found = system.findOnPath(keysExeName());
  if (found.isEmpty()) {
    result.lines << QStringLiteral(
                        "warning: wakukeys.exe is not on your PATH, so the keys daemon is not "
                        "registered; add %1 to your user PATH, sign out and in again, then run "
                        "this command again")
                        .arg(QDir::toNativeSeparators(system.ownFolder()));
    return true;
  }
  if (normalizedFolder(QFileInfo(found).path()) != normalizedFolder(system.ownFolder())) {
    result.lines << QStringLiteral(
                        "warning: wakukeys.exe on your PATH is %1, not the one next to wakuc in "
                        "%2; the task starts the one on the PATH")
                        .arg(QDir::toNativeSeparators(found),
                             QDir::toNativeSeparators(system.ownFolder()));
  }
  // No --config: wakukeys reads its default file itself, which is the file checked above.
  const QString arguments = commandLine(keysArguments(options.configHome));
  const QString error = system.registerTask(Task::Keys, arguments);
  if (!error.isEmpty()) {
    result.lines << QStringLiteral("keys daemon: %1").arg(error);
    return false;
  }
  result.lines << QStringLiteral("startup: keys daemon enabled, %1")
                      .arg(programWithArguments(keysExeName(), arguments));
  return true;
}

}  // namespace

QStringList wakuArguments(const StartupOptions& options) {
  QStringList out;
  if (!options.configHome.isEmpty()) out << QStringLiteral("--config-home") << options.configHome;
  if (options.registerAppBar) out << QStringLiteral("--register-app-bar");
  if (options.hideTaskbar) out << QStringLiteral("--hide-taskbar");
  const QStringList present = out;
  for (const QString& extra : options.extraArguments) {
    if (extra.startsWith(QLatin1Char('-')) && present.contains(extra)) continue;
    out << extra;
  }
  return out;
}

QStringList keysArguments(const QString& configHome) {
  QStringList out;
  if (!configHome.isEmpty()) out << QStringLiteral("--config-home") << configHome;
  return out;
}

QString commandLine(const QStringList& arguments) {
  QStringList quoted;
  for (const QString& argument : arguments) quoted << quoteArgument(argument);
  return quoted.join(QLatin1Char(' '));
}

StartupResult enableStartup(StartupSystem& system, const StartupOptions& options) {
  StartupResult result;
  const QString own = QDir::toNativeSeparators(system.ownFolder());
  const QString found = system.findOnPath(exeName());
  if (found.isEmpty()) {
    result.lines << QStringLiteral(
                        "waku.exe is not on your PATH. Add %1 to your user PATH, sign out and in "
                        "again, then run this command again.")
                        .arg(own);
    return result;
  }
  if (normalizedFolder(QFileInfo(found).path()) != normalizedFolder(system.ownFolder())) {
    result.lines << QStringLiteral(
                        "warning: waku.exe on your PATH is %1, not the one next to wakuc in %2; "
                        "the task starts the one on the PATH")
                        .arg(QDir::toNativeSeparators(found), own);
  }
  const QString arguments = commandLine(wakuArguments(options));
  const QString error = system.registerTask(Task::Bar, arguments);
  if (!error.isEmpty()) {
    result.lines << error;
    result.ok = false;
    return result;
  }
  if (system.removeShortcut()) {
    result.lines << QStringLiteral("startup: deleted waku.lnk from your Startup folder");
  }
  result.lines << QStringLiteral("startup: enabled, %1").arg(programWithArguments(arguments));
  result.ok = enableKeys(system, options, result);
  return result;
}

StartupResult disableStartup(StartupSystem& system) {
  StartupResult result;
  const bool wasRegistered = system.queryTask(Task::Bar).registered;
  const bool keysWasRegistered = system.queryTask(Task::Keys).registered;
  QString error = system.removeTask(Task::Bar);
  if (error.isEmpty()) error = system.removeTask(Task::Keys);
  if (!error.isEmpty()) {
    result.lines << error;
    return result;
  }
  result.lines << (wasRegistered ? QStringLiteral("startup: disabled")
                                 : QStringLiteral("startup: not enabled"));
  if (keysWasRegistered) result.lines << QStringLiteral("startup: keys daemon disabled");
  result.ok = true;
  return result;
}

namespace {

// The --config-home of a registered command line; empty when it has none.
QString configHomeOf(const QString& arguments) {
  static const QRegularExpression pattern(
      QStringLiteral("--config-home\\s+(?:\"([^\"]+)\"|(\\S+))"));
  const QRegularExpressionMatch match = pattern.match(arguments);
  if (!match.hasMatch()) return {};
  return match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
}

}  // namespace

StartupResult startupStatus(StartupSystem& system) {
  StartupResult result;
  result.ok = true;
  const StartupSystem::TaskInfo task = system.queryTask(Task::Bar);
  if (!task.registered) {
    result.lines << QStringLiteral("startup: not registered");
  } else {
    result.lines << (task.enabled ? QStringLiteral("startup: enabled")
                                  : QStringLiteral("startup: disabled"));
    result.lines << QStringLiteral("command: %1").arg(programWithArguments(task.arguments));
  }
  const QString found = system.findOnPath(exeName());
  if (found.isEmpty()) {
    result.lines << QStringLiteral("waku.exe: not on PATH (add %1)")
                        .arg(QDir::toNativeSeparators(system.ownFolder()));
  } else {
    result.lines << QStringLiteral("waku.exe: %1").arg(QDir::toNativeSeparators(found));
  }
  const StartupSystem::TaskInfo keys = system.queryTask(Task::Keys);
  const QString configHome = configHomeOf(keys.arguments);
  if (!keys.registered) {
    result.lines << QStringLiteral("keys startup: not registered");
  } else {
    result.lines << (keys.enabled ? QStringLiteral("keys startup: enabled")
                                  : QStringLiteral("keys startup: disabled"));
    result.lines << QStringLiteral("keys command: %1")
                        .arg(programWithArguments(keysExeName(), keys.arguments));
  }
  const QString keysFound = system.findOnPath(keysExeName());
  result.lines
      << (keysFound.isEmpty()
              ? QStringLiteral("wakukeys.exe: not on PATH (add %1)")
                    .arg(QDir::toNativeSeparators(system.ownFolder()))
              : QStringLiteral("wakukeys.exe: %1").arg(QDir::toNativeSeparators(keysFound)));
  result.lines << QStringLiteral("keys file: %1 (%2)")
                      .arg(QDir::toNativeSeparators(system.keysConfigPath(configHome)),
                           system.keysConfigExists(configHome) ? QStringLiteral("found")
                                                               : QStringLiteral("missing"));
  return result;
}

}  // namespace waku::startup
