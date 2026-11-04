#include "features/startup/startup_service.h"

#include <QDir>
#include <QFileInfo>

namespace waku::startup {

namespace {

QString exeName() { return QStringLiteral("waku.exe"); }

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

QString programWithArguments(const QString& arguments) {
  return arguments.isEmpty() ? exeName() : exeName() + QLatin1Char(' ') + arguments;
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
  const QString error = system.registerTask(arguments);
  if (!error.isEmpty()) {
    result.lines << error;
    result.ok = false;
    return result;
  }
  if (system.removeShortcut()) {
    result.lines << QStringLiteral("startup: deleted waku.lnk from your Startup folder");
  }
  result.lines << QStringLiteral("startup: enabled, %1").arg(programWithArguments(arguments));
  result.ok = true;
  return result;
}

StartupResult disableStartup(StartupSystem& system) {
  StartupResult result;
  const bool wasRegistered = system.queryTask().registered;
  const QString error = system.removeTask();
  if (!error.isEmpty()) {
    result.lines << error;
    return result;
  }
  result.lines << (wasRegistered ? QStringLiteral("startup: disabled")
                                 : QStringLiteral("startup: not enabled"));
  result.ok = true;
  return result;
}

StartupResult startupStatus(StartupSystem& system) {
  StartupResult result;
  result.ok = true;
  const StartupSystem::TaskInfo task = system.queryTask();
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
  return result;
}

}  // namespace waku::startup
