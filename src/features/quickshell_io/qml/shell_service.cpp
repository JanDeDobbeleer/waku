#include "features/quickshell_io/qml/shell_service.h"

#include <QDir>
#include <QProcess>

#include "kernel/win32/shell_launch.h"

namespace waku::quickshell_io {

namespace {

bool startDetached(const QString& program, const QStringList& arguments) {
  QProcess process;
  process.setProgram(program);
  process.setArguments(arguments);
  kernel::hideConsole(process);
  return process.startDetached();
}

QString defaultConfigHome() {
  if (const QString fromEnv = qEnvironmentVariable("WAKU_CONFIG_HOME"); !fromEnv.isEmpty()) {
    return QDir::toNativeSeparators(fromEnv);
  }
  return QDir::toNativeSeparators(QDir::homePath() + QStringLiteral("/.config/waku"));
}

}  // namespace

Shell::Shell(QObject* parent)
    : Shell(Launcher{startDetached, kernel::launchHiddenShell,
                     [](const QString& target) { kernel::shellOpen(target); }},
            defaultConfigHome(), parent) {}

Shell::Shell(Launcher launcher, QString configHome, QObject* parent)
    : QObject(parent), launcher_(std::move(launcher)), configHome_(std::move(configHome)) {}

QString Shell::homeDir() const { return QDir::toNativeSeparators(QDir::homePath()); }

bool Shell::start(const QString& program, const QStringList& arguments) {
  if (program.isEmpty()) return false;
  return launcher_.start(program, arguments);
}

bool Shell::command(const QString& commandLine) {
  if (commandLine.trimmed().isEmpty()) return false;
  return launcher_.command(commandLine);
}

void Shell::open(const QString& target) {
  if (target.trimmed().isEmpty()) return;
  launcher_.open(target);
}

QString Shell::env(const QString& name) const { return qEnvironmentVariable(qPrintable(name)); }

}  // namespace waku::quickshell_io
