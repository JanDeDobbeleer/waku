#include "features/quickshell_io/command_spec.h"

#include <QMetaType>
#include <QProcess>
#include <utility>

#include "kernel/win32/shell_launch.h"

namespace waku::quickshell_io {

namespace {

QStringList toStrings(const QVariant& value) {
  QStringList out;
  const QVariantList list = value.toList();
  out.reserve(list.size());
  for (const QVariant& item : list) out.append(item.toString());
  return out;
}

bool isNull(const QVariant& value) {
  return !value.isValid() || value.metaType().id() == QMetaType::Nullptr;
}

}  // namespace

std::optional<CommandSpec> commandSpecFrom(const QVariant& value) {
  CommandSpec spec;
  if (value.metaType().id() == QMetaType::QVariantMap) {
    const QVariantMap map = value.toMap();
    spec.command = toStrings(map.value(QStringLiteral("command")));
    if (const auto it = map.constFind(QStringLiteral("environment")); it != map.constEnd()) {
      spec.environment = it->toMap();
    }
    spec.clearEnvironment = map.value(QStringLiteral("clearEnvironment")).toBool();
    spec.workingDirectory = map.value(QStringLiteral("workingDirectory")).toString();
    spec.elevated = map.value(QStringLiteral("elevated")).toBool();
  } else if (value.metaType().id() != QMetaType::QString && value.canConvert<QVariantList>()) {
    spec.command = toStrings(value);
  } else {
    return std::nullopt;
  }
  if (spec.command.isEmpty()) return std::nullopt;
  return spec;
}

QProcessEnvironment makeEnvironment(const CommandSpec& spec) {
  const QProcessEnvironment system = QProcessEnvironment::systemEnvironment();
  QProcessEnvironment env = spec.clearEnvironment ? QProcessEnvironment() : system;
  for (auto it = spec.environment.cbegin(); it != spec.environment.cend(); ++it) {
    if (isNull(it.value())) {
      if (spec.clearEnvironment) {
        if (system.contains(it.key())) env.insert(it.key(), system.value(it.key()));
      } else {
        env.remove(it.key());
      }
    } else {
      env.insert(it.key(), it.value().toString());
    }
  }
  return env;
}

QString windowsArguments(const QStringList& arguments) {
  QString line;
  for (const QString& argument : arguments) {
    if (!line.isEmpty()) line += QLatin1Char(' ');
    const bool plain = !argument.isEmpty() && !argument.contains(QLatin1Char(' ')) &&
                       !argument.contains(QLatin1Char('\t')) &&
                       !argument.contains(QLatin1Char('"'));
    if (plain) {
      line += argument;
      continue;
    }
    line += QLatin1Char('"');
    qsizetype backslashes = 0;
    for (const QChar c : argument) {
      if (c == QLatin1Char('\\')) {
        ++backslashes;
        continue;
      }
      // Backslashes before a quote are doubled, and the quote is escaped.
      if (c == QLatin1Char('"'))
        line += QString(backslashes * 2 + 1, QLatin1Char('\\'));
      else
        line += QString(backslashes, QLatin1Char('\\'));
      backslashes = 0;
      line += c;
    }
    // Backslashes before the closing quote are doubled.
    line += QString(backslashes * 2, QLatin1Char('\\'));
    line += QLatin1Char('"');
  }
  return line;
}

bool startDetachedSpec(const CommandSpec& spec) {
  if (spec.command.isEmpty()) return false;
  if (spec.elevated) return startElevated(spec);
  QProcess process;
  process.setProgram(spec.command.first());
  process.setArguments(spec.command.mid(1));
  process.setProcessEnvironment(makeEnvironment(spec));
  if (!spec.workingDirectory.isEmpty()) process.setWorkingDirectory(spec.workingDirectory);
  kernel::hideConsole(process);
  return process.startDetached();
}

}  // namespace waku::quickshell_io
